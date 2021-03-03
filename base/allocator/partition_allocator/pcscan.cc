// Copyright (c) 2020 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/allocator/partition_allocator/pcscan.h"

#include <algorithm>
#include <condition_variable>
#include <mutex>
#include <numeric>
#include <set>
#include <thread>
#include <vector>

#include "base/allocator/partition_allocator/address_pool_manager.h"
#include "base/allocator/partition_allocator/object_bitmap.h"
#include "base/allocator/partition_allocator/page_allocator.h"
#include "base/allocator/partition_allocator/page_allocator_constants.h"
#include "base/allocator/partition_allocator/partition_address_space.h"
#include "base/allocator/partition_allocator/partition_alloc.h"
#include "base/allocator/partition_allocator/partition_alloc_check.h"
#include "base/allocator/partition_allocator/partition_alloc_constants.h"
#include "base/allocator/partition_allocator/partition_alloc_features.h"
#include "base/allocator/partition_allocator/partition_page.h"
#include "base/compiler_specific.h"
#include "base/logging.h"
#include "base/no_destructor.h"
#include "base/threading/platform_thread.h"
#include "base/trace_event/base_tracing.h"

namespace base {
namespace internal {

namespace {

ThreadSafePartitionRoot& PCScanMetadataAllocator() {
  static base::NoDestructor<ThreadSafePartitionRoot> allocator{
      PartitionOptions{PartitionOptions::Alignment::kRegular,
                       PartitionOptions::ThreadCache::kDisabled,
                       PartitionOptions::PCScan::kAlwaysDisabled}};
  return *allocator;
}

// STL allocator which is needed to keep internal data structures required by
// PCScan.
template <typename T>
class MetadataAllocator {
 public:
  using value_type = T;

  MetadataAllocator() = default;

  template <typename U>
  MetadataAllocator(const MetadataAllocator<U>&) {}  // NOLINT

  template <typename U>
  MetadataAllocator& operator=(const MetadataAllocator<U>&) {}

  template <typename U>
  bool operator==(const MetadataAllocator<U>&) {
    return true;
  }

  value_type* allocate(size_t size) {
    return static_cast<value_type*>(PCScanMetadataAllocator().AllocFlagsNoHooks(
        0, size * sizeof(value_type)));
  }

  void deallocate(value_type* ptr, size_t size) {
    PCScanMetadataAllocator().FreeNoHooks(ptr);
  }
};

void ReportStats(size_t swept_bytes, size_t last_size, size_t new_size) {
  VLOG(2) << "quarantine size: " << last_size << " -> " << new_size
          << ", swept bytes: " << swept_bytes
          << ", survival rate: " << static_cast<double>(new_size) / last_size;
}

template <bool thread_safe>
uintptr_t GetObjectStartInSuperPage(uintptr_t maybe_ptr,
                                    const PartitionRoot<thread_safe>& root) {
  char* allocation_start =
      GetSlotStartInSuperPage<thread_safe>(reinterpret_cast<char*>(maybe_ptr));
  if (!allocation_start) {
    // |maybe_ptr| refers to a garbage or is outside of the payload region.
    return 0;
  }
  return reinterpret_cast<uintptr_t>(
      root.AdjustPointerForExtrasAdd(allocation_start));
}

namespace scopes {
constexpr char kPCScan[] = "PCScan";
constexpr char kClear[] = "PCScan.Clear";
constexpr char kScan[] = "PCScan.Scan";
constexpr char kSweep[] = "PCScan.Sweep";
}  // namespace scopes
constexpr char kTraceCategory[] = "partition_alloc";
#define PCSCAN_EVENT(scope) TRACE_EVENT0(kTraceCategory, (scope))

}  // namespace

// This class is responsible for performing the entire PCScan task.
template <bool thread_safe>
class PCScan<thread_safe>::PCScanTask final {
 public:
  static void* operator new(size_t size) {
    return PCScanMetadataAllocator().AllocFlagsNoHooks(0, size);
  }

  static void operator delete(void* ptr) {
    PCScanMetadataAllocator().FreeNoHooks(ptr);
  }

  // Creates and initializes a PCScan state.
  explicit PCScanTask(PCScan& pcscan);

  // Only allow moving to make sure that the state is not redundantly copied.
  PCScanTask(PCScanTask&&) noexcept = default;
  PCScanTask& operator=(PCScanTask&&) noexcept = default;

  // Execute PCScan. Must be executed only once.
  void RunOnce() &&;

 private:
  using SlotSpan = SlotSpanMetadata<thread_safe>;

  struct ScanArea {
    ScanArea(uintptr_t* begin, uintptr_t* end) : begin(begin), end(end) {}

    uintptr_t* begin;
    uintptr_t* end;
  };
  using ScanAreas = std::vector<ScanArea, MetadataAllocator<ScanArea>>;

  // Large scan areas have their slot size recorded which allows to iterate
  // based on objects, potentially skipping over objects if possible.
  struct LargeScanArea : public ScanArea {
    LargeScanArea(uintptr_t* begin, uintptr_t* end, size_t slot_size)
        : ScanArea(begin, end), slot_size(slot_size) {}

    size_t slot_size = 0;
  };
  using LargeScanAreas =
      std::vector<LargeScanArea, MetadataAllocator<LargeScanArea>>;

  // Super pages only correspond to normal buckets.
  // TODO(bikineev): Consider flat containers since the number of elements is
  // relatively small. This requires making base containers allocator-aware.
  using SuperPages =
      std::set<uintptr_t, std::less<>, MetadataAllocator<uintptr_t>>;

  class SuperPagesBitmap final {
   public:
    void Populate(const SuperPages& super_pages) {
      for (uintptr_t super_page_base : super_pages) {
        PA_DCHECK(!(super_page_base % kSuperPageAlignment));
        PA_DCHECK(IsManagedByPartitionAllocNormalBuckets(
            reinterpret_cast<char*>(super_page_base)));
        bitset_.set((super_page_base - normal_bucket_pool_base_) >>
                    kSuperPageShift);
      }
    }

    ALWAYS_INLINE bool Test(uintptr_t maybe_ptr) const {
#if defined(PA_HAS_64_BITS_POINTERS)
      PA_DCHECK(IsManagedByPartitionAllocNormalBuckets(
          reinterpret_cast<char*>(maybe_ptr)));
#endif
      return bitset_.test(static_cast<size_t>(
          (maybe_ptr - normal_bucket_pool_base_) >> kSuperPageShift));
    }

   private:
    static constexpr size_t kBitmapSize =
        AddressPoolManager::kNormalBucketMaxSize >> kSuperPageShift;

    std::bitset<kBitmapSize> bitset_;
    const uintptr_t normal_bucket_pool_base_ =
#if defined(PA_HAS_64_BITS_POINTERS)
        PartitionAddressSpace::NormalBucketPoolBase();
#else
        0;
#endif
  };

  struct BitmapLookupPolicy {
    ALWAYS_INLINE bool TestPointer(uintptr_t maybe_ptr) const {
#if defined(PA_HAS_64_BITS_POINTERS)
      // First, do a fast bitmask check to see if the pointer points to the
      // normal bucket pool.
      if (!PartitionAddressSpace::IsInNormalBucketPool(
              reinterpret_cast<void*>(maybe_ptr)))
        return false;
#endif
      return task_.super_pages_bitmap_.Test(maybe_ptr);
    }
    const PCScanTask& task_;
  };

  struct BinaryLookupPolicy {
    ALWAYS_INLINE bool TestPointer(uintptr_t maybe_ptr) const {
      const auto super_page_base = maybe_ptr & kSuperPageBaseMask;
      auto it = task_.super_pages_.lower_bound(super_page_base);
      return it != task_.super_pages_.end() && *it == super_page_base;
    }
    const PCScanTask& task_;
  };

  template <class LookupPolicy>
  ALWAYS_INLINE QuarantineBitmap* TryFindScannerBitmapForPointer(
      uintptr_t maybe_ptr) const;

  // Lookup and marking functions. Return size of the object if marked or zero
  // otherwise.
  template <class LookupPolicy>
  ALWAYS_INLINE size_t TryMarkObjectInNormalBucketPool(uintptr_t maybe_ptr);

  // Scans all registeres partitions and marks reachable quarantined objects.
  // Returns the size of marked objects.
  template <class LookupPolicy>
  size_t ScanPartitions();

  // Scans a range of addresses and marks reachable quarantined objects. Returns
  // the size of marked objects. The function race-fully reads the heap and
  // therefore TSAN is disabled for it.
  template <class LookupPolicy>
  size_t ScanRange(Root* root, uintptr_t* begin, uintptr_t* end)
      NO_SANITIZE("thread");

  // Clear quarantined objects inside the PCScan task.
  void ClearQuarantinedObjects() const;

  // Sweeps (frees) unreachable quarantined entries. Returns the size of swept
  // objects.
  size_t SweepQuarantine();

  PCScan<thread_safe>& pcscan_;

  ScanAreas scan_areas_;
  LargeScanAreas large_scan_areas_;
  SuperPages super_pages_;
  SuperPagesBitmap super_pages_bitmap_;
};

template <bool thread_safe>
template <class LookupPolicy>
ALWAYS_INLINE QuarantineBitmap*
PCScan<thread_safe>::PCScanTask::TryFindScannerBitmapForPointer(
    uintptr_t maybe_ptr) const {
  // First, check if |maybe_ptr| points to a valid super page.
  LookupPolicy lookup{*this};
  if (!lookup.TestPointer(maybe_ptr))
    return nullptr;
  // Check if we are not pointing to metadata/guard pages.
  if (!IsWithinSuperPagePayload(reinterpret_cast<char*>(maybe_ptr),
                                true /*with pcscan*/))
    return nullptr;
  // We are certain here that |maybe_ptr| points to the super page payload.
  return QuarantineBitmapFromPointer(QuarantineBitmapType::kScanner,
                                     pcscan_.quarantine_data_.epoch(),
                                     reinterpret_cast<char*>(maybe_ptr));
}

// Looks up and marks a potential dangling pointer. Returns the size of the slot
// (which is then accounted as quarantined) or zero if no object is found.
// For normal bucket super pages, PCScan uses two quarantine bitmaps, the
// mutator and the scanner one. The former is used by mutators when objects are
// freed, while the latter is used concurrently by the PCScan thread. The
// bitmaps are swapped as soon as PCScan is triggered. Once a dangling pointer
// (which points to an object in the scanner bitmap) is found,
// TryMarkObjectInNormalBucketPool() marks it again in the bitmap and clears
// from the scanner bitmap. This way, when scanning is done, all uncleared
// entries in the scanner bitmap correspond to unreachable objects.
template <bool thread_safe>
template <class LookupPolicy>
ALWAYS_INLINE size_t
PCScan<thread_safe>::PCScanTask::TryMarkObjectInNormalBucketPool(
    uintptr_t maybe_ptr) {
  // Check if maybe_ptr points somewhere to the heap.
  auto* bitmap = TryFindScannerBitmapForPointer<LookupPolicy>(maybe_ptr);
  if (!bitmap)
    return 0;

  auto* root =
      Root::FromPointerInNormalBucketPool(reinterpret_cast<char*>(maybe_ptr));

  // Check if pointer was in the quarantine bitmap.
  const uintptr_t base =
      GetObjectStartInSuperPage<thread_safe>(maybe_ptr, *root);
  if (!base || !bitmap->CheckBit(base))
    return 0;

  PA_DCHECK((maybe_ptr & kSuperPageBaseMask) == (base & kSuperPageBaseMask));

  auto target_slot_span =
      SlotSpan::FromPointerNoAlignmentCheck(reinterpret_cast<void*>(base));
  PA_DCHECK(root == PartitionRoot<thread_safe>::FromSlotSpan(target_slot_span));

  const size_t usable_size = root->AdjustSizeForExtrasSubtract(
      target_slot_span->GetUtilizedSlotSize());
  // Range check for inner pointers.
  if (maybe_ptr >= base + usable_size)
    return 0;

  // Now we are certain that |maybe_ptr| is a dangling pointer. Mark it again in
  // the mutator bitmap and clear from the scanner bitmap.
  bitmap->ClearBit(base);
  QuarantineBitmapFromPointer(QuarantineBitmapType::kMutator,
                              pcscan_.quarantine_data_.epoch(),
                              reinterpret_cast<char*>(base))
      ->SetBit(base);
  return target_slot_span->bucket->slot_size;
}

template <bool thread_safe>
void PCScan<thread_safe>::PCScanTask::ClearQuarantinedObjects() const {
  PCSCAN_EVENT(scopes::kClear);
  for (auto super_page : super_pages_) {
    auto* bitmap = QuarantineBitmapFromPointer(
        QuarantineBitmapType::kScanner, pcscan_.quarantine_data_.epoch(),
        reinterpret_cast<char*>(super_page));
    auto* root = Root::FromSuperPage(reinterpret_cast<char*>(super_page));
    bitmap->Iterate([root](uintptr_t ptr) {
      auto* object = reinterpret_cast<void*>(ptr);
      auto* slot_span = SlotSpan::FromPointerNoAlignmentCheck(object);
      // Use zero as a zapping value to speed up the fast bailout check in
      // ScanPartitions.
      memset(
          object, 0,
          root->AdjustSizeForExtrasSubtract(slot_span->GetUtilizedSlotSize()));
    });
  }
}

template <bool thread_safe>
template <class LookupPolicy>
size_t NO_SANITIZE("thread") PCScan<thread_safe>::PCScanTask::ScanRange(
    Root* root,
    uintptr_t* begin,
    uintptr_t* end) {
  static_assert(alignof(uintptr_t) % alignof(void*) == 0,
                "Alignment of uintptr_t must be at least as strict as "
                "alignment of a pointer type.");
  size_t new_quarantine_size = 0;

  for (uintptr_t* payload = begin; payload < end; ++payload) {
    PA_DCHECK(reinterpret_cast<uintptr_t>(payload) % alignof(void*) == 0);
    auto maybe_ptr = *payload;
    if (!maybe_ptr)
      continue;
    new_quarantine_size +=
        TryMarkObjectInNormalBucketPool<LookupPolicy>(maybe_ptr);
  }

  return new_quarantine_size;
}

template <bool thread_safe>
template <class LookupPolicy>
size_t PCScan<thread_safe>::PCScanTask::ScanPartitions() {
  PCSCAN_EVENT(scopes::kScan);

  size_t new_quarantine_size = 0;
  // For scanning large areas, it's worthwhile checking whether the range that
  // is scanned contains quarantined objects.
  for (auto scan_area : large_scan_areas_) {
    // The bitmap is (a) always guaranteed to exist and (b) the same for all
    // objects in a given slot span.
    // TODO(chromium:1129751): Check mutator bitmap as well if performance
    // allows.
    auto* root = Root::FromPointerInNormalBucketPool(
        reinterpret_cast<char*>(scan_area.begin));
    auto* bitmap = QuarantineBitmapFromPointer(
        QuarantineBitmapType::kScanner, pcscan_.quarantine_data_.epoch(),
        reinterpret_cast<char*>(scan_area.begin));
    for (uintptr_t current_slot = reinterpret_cast<uintptr_t>(scan_area.begin);
         current_slot < reinterpret_cast<uintptr_t>(scan_area.end);
         current_slot += scan_area.slot_size) {
      // It is okay to skip objects as their payload has been zapped at this
      // point which means that the pointers no longer retain other objects.
      if (bitmap->CheckBit(current_slot)) {
        continue;
      }
      uintptr_t* payload_end =
          reinterpret_cast<uintptr_t*>(current_slot + scan_area.slot_size);
      PA_DCHECK(payload_end <= scan_area.end);
      new_quarantine_size += ScanRange<LookupPolicy>(
          root, reinterpret_cast<uintptr_t*>(current_slot), payload_end);
    }
  }
  for (auto scan_area : scan_areas_) {
    auto* root = Root::FromPointerInNormalBucketPool(
        reinterpret_cast<char*>(scan_area.begin));
    new_quarantine_size +=
        ScanRange<LookupPolicy>(root, scan_area.begin, scan_area.end);
  }
  return new_quarantine_size;
}

template <bool thread_safe>
size_t PCScan<thread_safe>::PCScanTask::SweepQuarantine() {
  PCSCAN_EVENT(scopes::kSweep);
  size_t swept_bytes = 0;

  for (auto super_page : super_pages_) {
    auto* bitmap = QuarantineBitmapFromPointer(
        QuarantineBitmapType::kScanner, pcscan_.quarantine_data_.epoch(),
        reinterpret_cast<char*>(super_page));
    auto* root = Root::FromSuperPage(reinterpret_cast<char*>(super_page));
    bitmap->Iterate([root, &swept_bytes](uintptr_t ptr) {
      auto* object = reinterpret_cast<void*>(ptr);
      auto* slot_span = SlotSpan::FromPointerNoAlignmentCheck(object);
      swept_bytes += slot_span->bucket->slot_size;
      root->FreeNoHooksImmediate(object, slot_span);
    });
    bitmap->Clear();
  }

  return swept_bytes;
}

template <bool thread_safe>
PCScan<thread_safe>::PCScanTask::PCScanTask(PCScan& pcscan) : pcscan_(pcscan) {
  // Threshold for which bucket size it is worthwhile in checking whether the
  // object is a quarantined object and can be skipped.
  static constexpr size_t kLargeScanAreaThreshold = 8192;
  // Take a snapshot of all allocated non-empty slot spans.
  static constexpr size_t kScanAreasReservationSize = 128;
  scan_areas_.reserve(kScanAreasReservationSize);

  for (Root* root : pcscan.roots_) {
    typename Root::ScopedGuard guard(root->lock_);

    // TODO: We should probably skip some roots if they are not highly
    // allocating/freeing. For that we would need to introduce and record some
    // LocalQuarantineData per partition.

    // Take a snapshot of all super pages and scannable slot spans.
    // TODO(bikineev): Consider making current_extent lock-free and moving it to
    // the concurrent thread.
    for (auto* super_page_extent = root->first_extent; super_page_extent;
         super_page_extent = super_page_extent->next) {
      for (char* super_page = super_page_extent->super_page_base;
           super_page != super_page_extent->super_pages_end;
           super_page += kSuperPageSize) {
        // TODO(bikineev): Consider following freelists instead of slot spans.
        IterateActiveAndFullSlotSpans<thread_safe>(
            super_page, true /*with pcscan*/, [this](SlotSpan* slot_span) {
              auto* payload_begin =
                  static_cast<uintptr_t*>(SlotSpan::ToPointer(slot_span));
              size_t provisioned_size = slot_span->GetProvisionedSize();
              // Free & decommitted slot spans are skipped.
              PA_DCHECK(provisioned_size > 0);
              auto* payload_end =
                  payload_begin + (provisioned_size / sizeof(uintptr_t));
              if (slot_span->bucket->slot_size >= kLargeScanAreaThreshold) {
                large_scan_areas_.push_back(
                    {payload_begin, payload_end, slot_span->bucket->slot_size});
              } else {
                scan_areas_.push_back({payload_begin, payload_end});
              }
            });
        super_pages_.insert(reinterpret_cast<uintptr_t>(super_page));
      }
    }
  }
}

template <bool thread_safe>
void PCScan<thread_safe>::PCScanTask::RunOnce() && {
  PCSCAN_EVENT(scopes::kPCScan);

  const bool is_with_gigacage = features::IsPartitionAllocGigaCageEnabled();
  if (is_with_gigacage) {
    // Prepare super page bitmap for fast scanning.
    super_pages_bitmap_.Populate(super_pages_);
  }

  // Clear all quarantined objects.
  ClearQuarantinedObjects();

  // Mark and sweep the quarantine list.
  const auto new_quarantine_size = is_with_gigacage
                                       ? ScanPartitions<BitmapLookupPolicy>()
                                       : ScanPartitions<BinaryLookupPolicy>();
  const auto swept_bytes = SweepQuarantine();

  ReportStats(swept_bytes, pcscan_.quarantine_data_.last_size(),
              new_quarantine_size);

  const size_t total_pa_heap_size =
      std::accumulate(pcscan_.roots_.begin(), pcscan_.roots_.end(), 0u,
                      [](size_t size, Root* root) {
                        return size + root->get_total_size_of_committed_pages();
                      });

  pcscan_.quarantine_data_.Account(new_quarantine_size);
  pcscan_.quarantine_data_.GrowLimitIfNeeded(total_pa_heap_size);

  // Check that concurrent task can't be scheduled twice.
  PA_CHECK(pcscan_.in_progress_.exchange(false));
}

template <bool thread_safe>
class PCScan<thread_safe>::PCScanThread final {
 public:
  using TaskHandle = std::unique_ptr<PCScanTask>;

  static PCScanThread& Instance() {
    // Lazily instantiate the scanning thread.
    static base::NoDestructor<PCScanThread> instance;
    return *instance;
  }

  void PostTask(TaskHandle task) {
    {
      std::lock_guard<std::mutex> lock(mutex_);
      PA_DCHECK(!posted_task_.get());
      posted_task_ = std::move(task);
    }
    condvar_.notify_all();
  }

 private:
  friend class base::NoDestructor<PCScanThread>;

  PCScanThread() {
    std::thread{[this] {
      static constexpr const char* kThreadName = "PCScan";
      // Ideally we should avoid mixing base:: and std:: API for threading, but
      // this is useful for visualizing the pcscan thread in chrome://tracing.
      base::PlatformThread::SetName(kThreadName);
      TaskLoop();
    }}.detach();
  }

  void TaskLoop() {
    while (true) {
      TaskHandle current_task;
      {
        std::unique_lock<std::mutex> lock(mutex_);
        condvar_.wait(lock, [this] { return posted_task_.get(); });
        std::swap(current_task, posted_task_);
      }
      std::move(*current_task).RunOnce();
    }
  }

  std::mutex mutex_;
  std::condition_variable condvar_;
  TaskHandle posted_task_;
};

template <bool thread_safe>
constexpr size_t PCScan<thread_safe>::QuarantineData::kQuarantineSizeMinLimit;

template <bool thread_safe>
void PCScan<thread_safe>::QuarantineData::ResetAndAdvanceEpoch() {
  last_size_ = current_size_.exchange(0, std::memory_order_relaxed);
  epoch_.fetch_add(1, std::memory_order_relaxed);
}

template <bool thread_safe>
void PCScan<thread_safe>::QuarantineData::GrowLimitIfNeeded(size_t heap_size) {
  static constexpr double kQuarantineSizeFraction = 0.1;
  // |heap_size| includes the current quarantine size, we intentionally leave
  // some slack till hitting the limit.
  size_limit_.store(
      std::max(kQuarantineSizeMinLimit,
               static_cast<size_t>(kQuarantineSizeFraction * heap_size)),
      std::memory_order_relaxed);
}

template <bool thread_safe>
void PCScan<thread_safe>::Roots::Add(Root* root) {
  PA_CHECK(std::find(begin(), end(), root) == end());
  (*this)[current_] = root;
  ++current_;
  PA_CHECK(current_ != kMaxNumberOfPartitions)
      << "Exceeded number of allowed partitions";
}

template <bool thread_safe>
void PCScan<thread_safe>::Roots::ClearForTesting() {
  std::fill(begin(), end(), nullptr);
  current_ = 0;
}

template <bool thread_safe>
void PCScan<thread_safe>::PerformScan(InvocationMode invocation_mode) {
  PA_DCHECK(roots_.size() > 0);
  PA_DCHECK(std::all_of(roots_.begin(), roots_.end(),
                        [](Root* root) { return root->IsScanEnabled(); }));

  if (in_progress_.exchange(true)) {
    // Bail out if PCScan is already in progress.
    return;
  }

  quarantine_data_.ResetAndAdvanceEpoch();

  // Initialize PCScan task.
  auto task = std::make_unique<PCScanTask>(*this);

  // Post PCScan task.
  if (LIKELY(invocation_mode == InvocationMode::kNonBlocking)) {
    PCScanThread::Instance().PostTask(std::move(task));
  } else {
    PA_DCHECK(InvocationMode::kBlocking == invocation_mode ||
              InvocationMode::kForcedBlocking == invocation_mode);
    std::move(*task).RunOnce();
  }
}

template <bool thread_safe>
void PCScan<thread_safe>::PerformScanIfNeeded(InvocationMode invocation_mode) {
  if (!roots_.size())
    return;
  if (invocation_mode == InvocationMode::kForcedBlocking ||
      quarantine_data_.MinimumScanningThresholdReached())
    PerformScan(invocation_mode);
}

template <bool thread_safe>
void PCScan<thread_safe>::RegisterRoot(Root* root) {
  // Commit quarantine bitmaps.
  size_t quarantine_bitmaps_size_to_commit = CommittedQuarantineBitmapsSize();
  for (auto* super_page_extent = root->first_extent; super_page_extent;
       super_page_extent = super_page_extent->next) {
    for (char* super_page = super_page_extent->super_page_base;
         super_page != super_page_extent->super_pages_end;
         super_page += kSuperPageSize) {
      RecommitSystemPages(internal::SuperPageQuarantineBitmaps(super_page),
                          quarantine_bitmaps_size_to_commit, PageReadWrite,
                          PageUpdatePermissions);
    }
  }
  roots_.Add(root);
}

template <bool thread_safe>
void PCScan<thread_safe>::ClearRootsForTesting() {
  roots_.ClearForTesting();  // IN-TEST
}

template <bool thread_safe>
PCScan<thread_safe> PCScan<thread_safe>::instance_ PA_CONSTINIT;

template class PCScan<ThreadSafe>;
template class PCScan<NotThreadSafe>;

}  // namespace internal
}  // namespace base
