// Copyright 2020 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chromeos/printing/ppd_metadata_manager.h"

#include <algorithm>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "base/containers/flat_map.h"
#include "base/containers/queue.h"
#include "base/containers/span.h"
#include "base/json/json_reader.h"
#include "base/logging.h"
#include "base/optional.h"
#include "base/sequence_checker.h"
#include "base/strings/strcat.h"
#include "base/strings/string_piece.h"
#include "base/strings/string_split.h"
#include "base/strings/stringprintf.h"
#include "base/time/clock.h"
#include "base/time/time.h"
#include "base/values.h"
#include "chromeos/printing/ppd_metadata_parser.h"
#include "chromeos/printing/ppd_provider.h"
#include "chromeos/printing/printer_config_cache.h"

namespace chromeos {

namespace {

// Defines the containing directory of all metadata in the serving root.
const char kMetadataParentDirectory[] = "metadata_v3";

// Defines the number of shards of sharded metadata.
constexpr int kNumShards = 20;

// Convenience struct containing parsed metadata of type T.
template <typename T>
struct ParsedMetadataWithTimestamp {
  base::Time time_of_parse;
  T value;
};

// Tracks the progress of a single call to
// PpdMetadataManager::FindAllEmmsAvailableInIndex().
class ForwardIndexSearchContext {
 public:
  ForwardIndexSearchContext(
      const std::vector<std::string>& emms,
      base::Time max_age,
      PpdMetadataManager::FindAllEmmsAvailableInIndexCallback cb)
      : emms_(emms), current_index_(), max_age_(max_age), cb_(std::move(cb)) {}
  ~ForwardIndexSearchContext() = default;

  ForwardIndexSearchContext(const ForwardIndexSearchContext&) = delete;
  ForwardIndexSearchContext& operator=(const ForwardIndexSearchContext&) =
      delete;

  ForwardIndexSearchContext(ForwardIndexSearchContext&&) = default;

  // The effective-make-and-model string currently being sought in the
  // forward index search tracked by this struct.
  base::StringPiece CurrentEmm() const {
    DCHECK_LT(current_index_, emms_.size());
    return emms_[current_index_];
  }

  // Returns whether the CurrentEmm() is the last one in |this|
  // that needs searching.
  bool CurrentEmmIsLast() const {
    DCHECK_LT(current_index_, emms_.size());
    return current_index_ + 1 == emms_.size();
  }

  void AdvanceToNextEmm() {
    DCHECK_LT(current_index_, emms_.size());
    current_index_++;
  }

  // Called when the PpdMetadataManager has searched all appropriate
  // forward index metadata for all |emms_|.
  void PostCallback() {
    DCHECK(CurrentEmmIsLast());
    base::SequencedTaskRunnerHandle::Get()->PostTask(
        FROM_HERE, base::BindOnce(std::move(cb_), cb_arg_));
  }

  // Called when the PpdMetadataManager successfully maps the
  // CurrentEmm() to a ParsedIndexValues struct.
  void AddDataFromForwardIndexForCurrentEmm(const ParsedIndexValues& value) {
    cb_arg_.insert_or_assign(CurrentEmm(), value);
  }

  base::Time MaxAge() const { return max_age_; }

 private:
  // List of all effective-make-and-model strings that caller gave to
  // PpdMetadataManager::FindAllEmmsAvailableInIndex().
  std::vector<std::string> emms_;

  // Index into |emms| that marks the effective-make-and-model string
  // currently being searched.
  size_t current_index_;

  // Freshness requirement for forward indices that this search reads.
  base::Time max_age_;

  // Callback that caller gave to
  // PpdMetadataManager::FindAllEmmsAvailableInIndex().
  PpdMetadataManager::FindAllEmmsAvailableInIndexCallback cb_;

  // Accrues data to pass to |cb|.
  base::flat_map<std::string, ParsedIndexValues> cb_arg_;
};

// Enqueues calls to PpdMetadataManager::FindAllEmmsAvailableInIndex().
class ForwardIndexSearchQueue {
 public:
  ForwardIndexSearchQueue() = default;
  ~ForwardIndexSearchQueue() = default;

  ForwardIndexSearchQueue(const ForwardIndexSearchQueue&) = delete;
  ForwardIndexSearchQueue& operator=(const ForwardIndexSearchQueue&) = delete;

  void Enqueue(ForwardIndexSearchContext context) {
    contexts_.push(std::move(context));
  }

  bool IsIdle() const { return contexts_.empty(); }

  ForwardIndexSearchContext& CurrentContext() {
    DCHECK(!IsIdle());
    return contexts_.front();
  }

  // Progresses the frontmost search context, advancing it to its
  // next effective-make-and-model string to find in forward index
  // metadata.
  //
  // If the frontmost search context has no more
  // effective-make-and-model strings to search, then
  // 1. its callback is posted from here and
  // 2. it is popped off the |contexts| queue.
  void AdvanceToNextEmm() {
    DCHECK(!IsIdle());
    if (CurrentContext().CurrentEmmIsLast()) {
      CurrentContext().PostCallback();
      contexts_.pop();
    } else {
      CurrentContext().AdvanceToNextEmm();
    }
  }

 private:
  base::queue<ForwardIndexSearchContext> contexts_;
};

// Maps parsed metadata by name to parsed contents.
//
// Implementation note: the keys (metadata names) used here are
// basenames attached to their containing directory - e.g.
// *  "metadata_v3/index-00.json"
// *  "metadata_v3/locales.json"
// This is done to match up with the PrinterConfigCache class and
// with the folder layout of the Chrome OS Printing serving root.
template <typename T>
using CachedParsedMetadataMap =
    base::flat_map<std::string, ParsedMetadataWithTimestamp<T>>;

// Returns whether |map| has a value for |key| fresher than
// |expiration|.
template <typename T>
bool MapHasValueFresherThan(const CachedParsedMetadataMap<T>& metadata_map,
                            base::StringPiece key,
                            base::Time expiration) {
  if (!metadata_map.contains(key)) {
    return false;
  }
  const auto& value = metadata_map.at(key);
  return value.time_of_parse > expiration;
}

// Calculates the shard number of |key| inside sharded metadata.
int IndexShard(base::StringPiece key) {
  unsigned int hash = 5381;
  for (char c : key) {
    hash = hash * 33 + c;
  }
  return hash % kNumShards;
}

// Helper class used by PpdMetadataManagerImpl::SetMetadataLocale().
// Sifts through the list of locales advertised by the Chrome OS
// Printing serving root and selects the best match for a
// particular browser locale.
//
// This class must not outlive any data it is fed.
// This class is neither copyable nor movable.
class MetadataLocaleFinder {
 public:
  explicit MetadataLocaleFinder(const std::string& browser_locale)
      : browser_locale_(browser_locale),
        browser_locale_pieces_(base::SplitStringPiece(browser_locale,
                                                      "-",
                                                      base::KEEP_WHITESPACE,
                                                      base::SPLIT_WANT_ALL)),
        is_english_available_(false) {}
  ~MetadataLocaleFinder() = default;

  MetadataLocaleFinder(const MetadataLocaleFinder&) = delete;
  MetadataLocaleFinder& operator=(const MetadataLocaleFinder&) = delete;

  // Finds and returns the best-fit metadata locale from |locales|.
  // Returns the empty string if no best candidate was found.
  base::StringPiece BestCandidate(base::span<const std::string> locales) {
    AnalyzeCandidates(locales);

    if (!best_parent_locale_.empty()) {
      return best_parent_locale_;
    } else if (!best_distant_relative_locale_.empty()) {
      return best_distant_relative_locale_;
    } else if (is_english_available_) {
      return "en";
    }
    return base::StringPiece();
  }

 private:
  // Returns whether or not |locale| appears to be a parent of our
  // |browser_locale_|. For example, "en-GB" is a parent of "en-GB-foo."
  bool IsParentOfBrowserLocale(base::StringPiece locale) const {
    const std::string locale_with_trailing_hyphen = base::StrCat({locale, "-"});
    return base::StringPiece(browser_locale_)
        .starts_with(locale_with_trailing_hyphen);
  }

  // Updates our |best_distant_relative_locale_| to |locale| if we find
  // that it's a better match.
  //
  // The best distant relative locale is the one that
  // *  has the longest piecewise match with |browser_locale_| but
  // *  has the shortest piecewise length.
  // So given a |browser_locale_| "es," the better distant relative
  // locale between "es-GB" and "es-GB-foo" is "es-GB."
  void AnalyzeCandidateAsDistantRelative(base::StringPiece locale) {
    const std::vector<base::StringPiece> locale_pieces = base::SplitStringPiece(
        locale, "-", base::KEEP_WHITESPACE, base::SPLIT_WANT_ALL);

    const size_t locale_piecewise_length = locale_pieces.size();
    const size_t iter_limit =
        std::min(browser_locale_pieces_.size(), locale_piecewise_length);
    size_t locale_piecewise_match_length = 0;
    for (; locale_piecewise_match_length < iter_limit;
         locale_piecewise_match_length++) {
      if (locale_pieces[locale_piecewise_match_length] !=
          browser_locale_pieces_[locale_piecewise_match_length]) {
        break;
      }
    }

    if (locale_piecewise_match_length == 0) {
      return;
    } else if (locale_piecewise_match_length >
                   best_distant_relative_locale_piecewise_match_length_ ||
               (locale_piecewise_match_length ==
                    best_distant_relative_locale_piecewise_match_length_ &&
                locale_piecewise_length <
                    best_distant_relative_locale_piecewise_length_)) {
      best_distant_relative_locale_ = std::string(locale);
      best_distant_relative_locale_piecewise_match_length_ =
          locale_piecewise_match_length;
      best_distant_relative_locale_piecewise_length_ = locale_piecewise_length;
    }
  }

  // Reads |locale| and updates our members as necessary.
  // For example, |locale| could reveal support for the "en" locale.
  void AnalyzeCandidate(base::StringPiece locale) {
    if (locale == "en") {
      is_english_available_ = true;
    }

    if (IsParentOfBrowserLocale(locale) &&
        locale.size() > best_parent_locale_.size()) {
      best_parent_locale_ = std::string(locale);
    } else if (best_parent_locale_.empty()) {
      // We need only track distant relative locales if we don't have a
      // |best_parent_locale_|, which is always a better choice.
      AnalyzeCandidateAsDistantRelative(locale);
    }
  }

  // Analyzes all candidate locales in |locales|, updating our
  // private members with best-fit locale(s).
  void AnalyzeCandidates(base::span<const std::string> locales) {
    for (base::StringPiece locale : locales) {
      // The serving root indicates direct support for our browser
      // locale; there's no need to analyze anything else, since this
      // is definitely the best match we're going to get.
      if (locale == browser_locale_) {
        best_parent_locale_ = std::string(browser_locale_);
        return;
      }
      AnalyzeCandidate(locale);
    }
  }

  const base::StringPiece browser_locale_;
  const std::vector<base::StringPiece> browser_locale_pieces_;

  // See IsParentOfBrowserLocale().
  std::string best_parent_locale_;

  // See AnalyzeCandidateAsDistantRelative().
  std::string best_distant_relative_locale_;
  size_t best_distant_relative_locale_piecewise_match_length_;
  size_t best_distant_relative_locale_piecewise_length_;

  // Denotes whether or not the Chrome OS Printing serving root serves
  // metadata for the "en" locale - our final fallback.
  bool is_english_available_;
};

enum class PpdMetadataType {
  LOCALES,
  MANUFACTURERS,  // locale-sensitive
  PRINTERS,       // locale-sensitive
  INDEX,
  REVERSE_INDEX,  // locale-sensitive
  USB_INDEX,
};

// Control argument that fully specifies the basename and containing
// directory of a single piece of PPD metadata.
//
// *  Fields should be populated appropriate to the |type|.
// *  Fields are selectively read or ignored by
//    PpdMetadataPathInServingRoot().
// *  This class must not outlive its |optional_tag|.
struct PpdMetadataPathSpecifier {
  PpdMetadataType type;

  // Used in two different ways as needed:
  // 1. if |type| == PRINTERS, caller should populate this with the full
  //    basename of the target printers metadata file. Or,
  // 2. if |type| is locale-sensitive and != PRINTERS, caller
  //    should populate this with the two-letter target locale (as
  //    previously advertised by the serving root).
  //
  // This member is a const char* rather than std::string or StringPiece
  // for compatibility with base::StringPrintf().
  const char* optional_tag;

  // Numerical shard of target metadata basename, if needed.
  int optional_shard;
};

// Names a single piece of metadata in the Chrome OS Printing serving
// root specified by |options| - i.e. a metadata basename and its
// enclosing directory (see comment for CachedParsedMetadataMap).
std::string PpdMetadataPathInServingRoot(
    const PpdMetadataPathSpecifier& options) {
  switch (options.type) {
    case PpdMetadataType::LOCALES:
      return base::StringPrintf("%s/locales.json", kMetadataParentDirectory);

    case PpdMetadataType::MANUFACTURERS:
      // This type is locale-sensitive; the tag carries the locale.
      DCHECK(!base::StringPiece(options.optional_tag).empty());
      return base::StringPrintf("%s/manufacturers-%s.json",
                                kMetadataParentDirectory, options.optional_tag);

    case PpdMetadataType::PRINTERS:
      // This type is locale-sensitive; in this context, the tag carries
      // the full basename, which caller will have extracted from a leaf
      // in manufacturers metadata.
      DCHECK(!base::StringPiece(options.optional_tag).empty());
      return base::StringPrintf("%s/%s", kMetadataParentDirectory,
                                options.optional_tag);

    case PpdMetadataType::INDEX:
      DCHECK(options.optional_shard >= 0 &&
             options.optional_shard < kNumShards);
      return base::StringPrintf("%s/index-%02d.json", kMetadataParentDirectory,
                                options.optional_shard);

    case PpdMetadataType::REVERSE_INDEX:
      // This type is locale-sensitive; the tag carries the locale.
      DCHECK(!base::StringPiece(options.optional_tag).empty());
      DCHECK(options.optional_shard >= 0 &&
             options.optional_shard < kNumShards);
      return base::StringPrintf("%s/reverse_index-%s-%02d.json",
                                kMetadataParentDirectory, options.optional_tag,
                                options.optional_shard);

    case PpdMetadataType::USB_INDEX:
      DCHECK(options.optional_shard >= 0 &&
             options.optional_shard < kNumShards);
      return base::StringPrintf("%s/usb-%04x.json", kMetadataParentDirectory,
                                options.optional_shard);
  }

  // This function cannot fail except by maintainer error.
  NOTREACHED();

  return std::string();
}

// Note: generally, each Get*() method is segmented into three parts:
// 1. check if query can be answered immediately,
// 2. fetch appropriate metadata if it can't [defer to On*Fetched()],
//    and (time passes)
// 3. answer query with appropriate metadata [call On*Available()].
class PpdMetadataManagerImpl : public PpdMetadataManager {
 public:
  PpdMetadataManagerImpl(base::StringPiece browser_locale,
                         base::Clock* clock,
                         std::unique_ptr<PrinterConfigCache> config_cache)
      : browser_locale_(browser_locale),
        clock_(clock),
        config_cache_(std::move(config_cache)),
        weak_factory_(this) {}

  ~PpdMetadataManagerImpl() override {
    DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  }

  void GetLocale(GetLocaleCallback cb) override {
    DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

    // Per header comment: if a best-fit metadata locale is already set,
    // we don't refresh it; we just immediately declare success.
    //
    // Side effect: classes composing |this| can call
    // SetLocaleForTesting() before composition and get this cop-out
    // for free.
    if (!metadata_locale_.empty()) {
      base::SequencedTaskRunnerHandle::Get()->PostTask(
          FROM_HERE, base::BindOnce(std::move(cb), true));
      return;
    }

    const PpdMetadataPathSpecifier options = {PpdMetadataType::LOCALES};
    const std::string metadata_name = PpdMetadataPathInServingRoot(options);

    PrinterConfigCache::FetchCallback fetch_cb =
        base::BindOnce(&PpdMetadataManagerImpl::OnLocalesFetched,
                       weak_factory_.GetWeakPtr(), std::move(cb));

    // We call Fetch() with a default-constructed TimeDelta(): "give
    // me the freshest possible locales metadata."
    config_cache_->Fetch(metadata_name, base::TimeDelta(), std::move(fetch_cb));
  }

  void GetManufacturers(base::TimeDelta age,
                        PpdProvider::ResolveManufacturersCallback cb) override {
    DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
    DCHECK(!metadata_locale_.empty());

    const PpdMetadataPathSpecifier options = {PpdMetadataType::MANUFACTURERS,
                                              metadata_locale_.c_str()};
    const std::string metadata_name = PpdMetadataPathInServingRoot(options);

    if (MapHasValueFresherThan(cached_manufacturers_, metadata_name,
                               clock_->Now() - age)) {
      OnManufacturersAvailable(metadata_name, std::move(cb));
      return;
    }

    PrinterConfigCache::FetchCallback fetch_cb =
        base::BindOnce(&PpdMetadataManagerImpl::OnManufacturersFetched,
                       weak_factory_.GetWeakPtr(), std::move(cb));
    config_cache_->Fetch(metadata_name, age, std::move(fetch_cb));
  }

  void GetPrinters(base::StringPiece manufacturer,
                   base::TimeDelta age,
                   GetPrintersCallback cb) override {
    DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
    DCHECK(!metadata_locale_.empty());

    const auto metadata_name = GetPrintersMetadataName(manufacturer);
    if (!metadata_name.has_value()) {
      base::SequencedTaskRunnerHandle::Get()->PostTask(
          FROM_HERE, base::BindOnce(std::move(cb), false, ParsedPrinters{}));
      return;
    }

    if (MapHasValueFresherThan(cached_printers_, metadata_name.value(),
                               clock_->Now() - age)) {
      OnPrintersAvailable(metadata_name.value(), std::move(cb));
      return;
    }

    PrinterConfigCache::FetchCallback fetch_cb =
        base::BindOnce(&PpdMetadataManagerImpl::OnPrintersFetched,
                       weak_factory_.GetWeakPtr(), std::move(cb));
    config_cache_->Fetch(metadata_name.value(), age, std::move(fetch_cb));
  }

  void FindAllEmmsAvailableInIndex(
      const std::vector<std::string>& emms,
      base::TimeDelta age,
      FindAllEmmsAvailableInIndexCallback cb) override {
    DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

    ForwardIndexSearchContext context(emms, clock_->Now() - age, std::move(cb));
    bool queue_was_idle = forward_index_search_queue_.IsIdle();
    forward_index_search_queue_.Enqueue(std::move(context));

    // If we are the prime movers, then we need to set the forward
    // index search in motion.
    if (queue_was_idle) {
      ContinueSearchingForwardIndices();
    }

    // If we're not the prime movers, then a search is already ongoing
    // and we need not provide extra impetus.
  }

  void SplitMakeAndModel(base::StringPiece effective_make_and_model,
                         base::TimeDelta age,
                         PpdProvider::ReverseLookupCallback cb) override {
    DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
    DCHECK(!metadata_locale_.empty());

    const PpdMetadataPathSpecifier reverse_index_options = {
        PpdMetadataType::REVERSE_INDEX, metadata_locale_.c_str(),
        IndexShard(effective_make_and_model)};
    const std::string metadata_name =
        PpdMetadataPathInServingRoot(reverse_index_options);

    if (MapHasValueFresherThan(cached_reverse_indices_, metadata_name,
                               clock_->Now() - age)) {
      OnReverseIndexAvailable(metadata_name, effective_make_and_model,
                              std::move(cb));
      return;
    }

    PrinterConfigCache::FetchCallback fetch_cb =
        base::BindOnce(&PpdMetadataManagerImpl::OnReverseIndexFetched,
                       weak_factory_.GetWeakPtr(),
                       std::string(effective_make_and_model), std::move(cb));
    config_cache_->Fetch(metadata_name, age, std::move(fetch_cb));
  }

  PrinterConfigCache* GetPrinterConfigCacheForTesting() const override {
    return config_cache_.get();
  }

  void SetLocaleForTesting(base::StringPiece locale) override {
    metadata_locale_ = std::string(locale);
  }

  // This method should read much the same as OnManufacturersFetched().
  bool SetManufacturersForTesting(
      base::StringPiece manufacturers_json) override {
    DCHECK(!metadata_locale_.empty());

    const auto parsed = ParseManufacturers(manufacturers_json);
    if (!parsed.has_value()) {
      return false;
    }

    // We need to name the manufacturers metadata manually to store it.
    const PpdMetadataPathSpecifier options = {PpdMetadataType::MANUFACTURERS,
                                              metadata_locale_.c_str()};
    const std::string manufacturers_name =
        PpdMetadataPathInServingRoot(options);

    ParsedMetadataWithTimestamp<ParsedManufacturers> value = {clock_->Now(),
                                                              parsed.value()};
    cached_manufacturers_.insert_or_assign(manufacturers_name, value);
    return true;
  }

  base::StringPiece ExposeMetadataLocaleForTesting() const override {
    return metadata_locale_;
  }

 private:
  // Denotes the status of an ongoing forward index search - see
  // FindAllEmmsAvailableInIndex().
  enum class ForwardIndexSearchStatus {
    // We called |config_cache_|::Fetch(). We provided a bound
    // callback that will resume the forward index search for us when
    // the fetch completes.
    kWillResumeOnFetchCompletion,

    // We did not call |config_cache_|::Fetch(), so |this| still has
    // control of the progression of the forward index search.
    kCanContinue,
  };

  // Called by OnLocalesFetched().
  // Continues a prior call to GetLocale().
  //
  // Attempts to set |metadata_locale_| given the advertised
  // |locales_list|. Returns true if successful and false if not.
  bool SetMetadataLocale(const std::vector<std::string>& locales_list) {
    // This class helps track all the locales that _could_ be good fits
    // given our |browser_locale_| but which are not exact matches.
    MetadataLocaleFinder locale_finder(browser_locale_);

    metadata_locale_ = std::string(locale_finder.BestCandidate(locales_list));
    return !metadata_locale_.empty();
  }

  // Called back by |config_cache_|.Fetch().
  // Continues a prior call to GetLocale().
  //
  // On successful |result|, parses and sets the |metadata_locale_|.
  // Calls |cb| with the |result|.
  void OnLocalesFetched(GetLocaleCallback cb,
                        const PrinterConfigCache::FetchResult& result) {
    DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

    if (!result.succeeded) {
      base::SequencedTaskRunnerHandle::Get()->PostTask(
          FROM_HERE, base::BindOnce(std::move(cb), false));
      return;
    }
    const auto parsed = ParseLocales(result.contents);
    if (!parsed.has_value()) {
      base::SequencedTaskRunnerHandle::Get()->PostTask(
          FROM_HERE, base::BindOnce(std::move(cb), false));
      return;
    }

    // SetMetadataLocale() _can_ fail, but that would be an
    // extraordinarily bad thing - i.e. that the Chrome OS Printing
    // serving root is itself in an invalid state.
    base::SequencedTaskRunnerHandle::Get()->PostTask(
        FROM_HERE,
        base::BindOnce(std::move(cb), SetMetadataLocale(parsed.value())));
  }

  // Called by one of
  // *  GetManufacturers() or
  // *  OnManufacturersFetched().
  // Continues a prior call to GetManufacturers().
  //
  // Invokes |cb| with success, providing it with a list of
  // manufacturers.
  void OnManufacturersAvailable(base::StringPiece metadata_name,
                                PpdProvider::ResolveManufacturersCallback cb) {
    const auto& parsed_manufacturers = cached_manufacturers_.at(metadata_name);
    std::vector<std::string> manufacturers_for_cb;
    for (const auto& iter : parsed_manufacturers.value) {
      manufacturers_for_cb.push_back(iter.first);
    }
    std::sort(manufacturers_for_cb.begin(), manufacturers_for_cb.end());
    base::SequencedTaskRunnerHandle::Get()->PostTask(
        FROM_HERE,
        base::BindOnce(std::move(cb), PpdProvider::CallbackResultCode::SUCCESS,
                       manufacturers_for_cb));
  }

  // Called by |config_cache_|.Fetch().
  // Continues a prior call to GetManufacturers().
  //
  // Parses and updates our cached map of manufacturers if |result|
  // indicates a successful fetch. Calls |cb| accordingly.
  void OnManufacturersFetched(PpdProvider::ResolveManufacturersCallback cb,
                              const PrinterConfigCache::FetchResult& result) {
    DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

    if (!result.succeeded) {
      base::SequencedTaskRunnerHandle::Get()->PostTask(
          FROM_HERE,
          base::BindOnce(std::move(cb),
                         PpdProvider::CallbackResultCode::SERVER_ERROR,
                         std::vector<std::string>{}));
      return;
    }

    const auto parsed = ParseManufacturers(result.contents);
    if (!parsed.has_value()) {
      base::SequencedTaskRunnerHandle::Get()->PostTask(
          FROM_HERE,
          base::BindOnce(std::move(cb),
                         PpdProvider::CallbackResultCode::INTERNAL_ERROR,
                         std::vector<std::string>{}));
      return;
    }

    ParsedMetadataWithTimestamp<ParsedManufacturers> value = {clock_->Now(),
                                                              parsed.value()};
    cached_manufacturers_.insert_or_assign(result.key, value);
    OnManufacturersAvailable(result.key, std::move(cb));
  }

  // Called by GetPrinters().
  // Returns the known name for the Printers metadata named by
  // |manufacturer|.
  base::Optional<std::string> GetPrintersMetadataName(
      base::StringPiece manufacturer) {
    const PpdMetadataPathSpecifier manufacturers_options = {
        PpdMetadataType::MANUFACTURERS, metadata_locale_.c_str()};
    const std::string manufacturers_metadata_name =
        PpdMetadataPathInServingRoot(manufacturers_options);
    if (!cached_manufacturers_.contains(manufacturers_metadata_name)) {
      // This is likely a bug: we don't have the expected manufacturers
      // metadata.
      return base::nullopt;
    }

    const ParsedMetadataWithTimestamp<ParsedManufacturers>& manufacturers =
        cached_manufacturers_.at(manufacturers_metadata_name);
    if (!manufacturers.value.contains(manufacturer)) {
      // This is likely a bug: we don't know about this manufacturer.
      return base::nullopt;
    }

    const PpdMetadataPathSpecifier printers_options = {
        PpdMetadataType::PRINTERS,
        manufacturers.value.at(manufacturer).c_str()};
    return PpdMetadataPathInServingRoot(printers_options);
  }

  // Called by one of
  // *  GetPrinters() or
  // *  OnPrintersFetched().
  // Continues a prior call to GetPrinters().
  //
  // Invokes |cb| with success, providing it a map of printers.
  void OnPrintersAvailable(base::StringPiece metadata_name,
                           GetPrintersCallback cb) {
    const auto& parsed_printers = cached_printers_.at(metadata_name);
    base::SequencedTaskRunnerHandle::Get()->PostTask(
        FROM_HERE, base::BindOnce(std::move(cb), true, parsed_printers.value));
  }

  // Called by |config_cache_|.Fetch().
  // Continues a prior call to GetPrinters().
  //
  // Parses and updates our cached map of printers if |result| indicates
  // a successful fetch. Calls |cb| accordingly.
  void OnPrintersFetched(GetPrintersCallback cb,
                         const PrinterConfigCache::FetchResult& result) {
    DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

    if (!result.succeeded) {
      base::SequencedTaskRunnerHandle::Get()->PostTask(
          FROM_HERE, base::BindOnce(std::move(cb), false, ParsedPrinters{}));
      return;
    }

    const auto parsed = ParsePrinters(result.contents);
    if (!parsed.has_value()) {
      base::SequencedTaskRunnerHandle::Get()->PostTask(
          FROM_HERE, base::BindOnce(std::move(cb), false, ParsedPrinters{}));
      return;
    }

    ParsedMetadataWithTimestamp<ParsedPrinters> value = {clock_->Now(),
                                                         parsed.value()};
    cached_printers_.insert_or_assign(result.key, value);
    OnPrintersAvailable(result.key, std::move(cb));
  }

  // Called when one unit of sufficiently fresh forward index metadata
  // is available. Seeks out the current effective-make-and-model string
  // in said metadata.
  void FindEmmInForwardIndex(base::StringPiece metadata_name) {
    // Caller must have verified that this index is already present (and
    // sufficiently fresh) before entering this method.
    DCHECK(cached_forward_indices_.contains(metadata_name));

    ForwardIndexSearchContext& context =
        forward_index_search_queue_.CurrentContext();

    const ParsedIndex& index = cached_forward_indices_.at(metadata_name).value;
    const auto& iter = index.find(context.CurrentEmm());
    if (iter != index.end()) {
      context.AddDataFromForwardIndexForCurrentEmm(iter->second);
    }

    forward_index_search_queue_.AdvanceToNextEmm();
  }

  // Called by |config_cache_|.Fetch().
  // Continues a prior call to FindAllEmmsAvailableInForwardIndex().
  //
  // Parses and updates our cached map of forward indices if |result|
  // indicates a successful fetch. Continues the action that
  // necessitated fetching the present forward index.
  void OnForwardIndexFetched(const PrinterConfigCache::FetchResult& result) {
    if (!result.succeeded) {
      // We failed to fetch the forward index containing the current
      // effective-make-and-model string. There's nothing we can do but
      // carry on, e.g. by moving to deal with the next emm.
      forward_index_search_queue_.AdvanceToNextEmm();
      ContinueSearchingForwardIndices();
      return;
    }

    const auto parsed = ParseForwardIndex(result.contents);
    if (!parsed.has_value()) {
      // Same drill as fetch failure above.
      forward_index_search_queue_.AdvanceToNextEmm();
      ContinueSearchingForwardIndices();
      return;
    }
    ParsedMetadataWithTimestamp<ParsedIndex> value = {clock_->Now(),
                                                      parsed.value()};
    cached_forward_indices_.insert_or_assign(result.key, value);
    ContinueSearchingForwardIndices();
  }

  // Works on searching the forward index for the current
  // effective-make-and-model string in the frontmost entry in the
  // forward index search queue.
  //
  // One invocation of this method ultimately processes exactly one
  // effective-make-and-model string: either we find it in some forward
  // index metadata or we don't.
  ForwardIndexSearchStatus SearchForwardIndicesForOneEmm() {
    const ForwardIndexSearchContext& context =
        forward_index_search_queue_.CurrentContext();
    const PpdMetadataPathSpecifier options = {PpdMetadataType::INDEX, nullptr,
                                              IndexShard(context.CurrentEmm())};
    const std::string forward_index_name =
        PpdMetadataPathInServingRoot(options);

    if (MapHasValueFresherThan(cached_forward_indices_, forward_index_name,
                               context.MaxAge())) {
      // We have the appropriate forward index metadata and it's fresh
      // enough to make a determination: is the current
      // effective-make-and-model string  present in this metadata?
      FindEmmInForwardIndex(forward_index_name);
      return ForwardIndexSearchStatus::kCanContinue;
    }

    // We don't have the appropriate forward index metadata. We need to
    // get it before we can determine if the current
    // effective-make-and-model string is present in it.
    //
    // PrinterConfigCache::Fetch() accepts a TimeDelta expressing the
    // maximum permissible age of the cached response; to simulate the
    // original TimeDelta that caller gave to
    // FindAllEmmsAvailableInIndex(), we find the delta between Now()
    // and the absolute time ceiling recorded in the
    // ForwardIndexSearchContext.
    auto callback =
        base::BindOnce(&PpdMetadataManagerImpl::OnForwardIndexFetched,
                       weak_factory_.GetWeakPtr());
    config_cache_->Fetch(forward_index_name, clock_->Now() - context.MaxAge(),
                         std::move(callback));
    return ForwardIndexSearchStatus::kWillResumeOnFetchCompletion;
  }

  // Continues working on the forward index search queue.
  void ContinueSearchingForwardIndices() {
    while (!forward_index_search_queue_.IsIdle()) {
      ForwardIndexSearchStatus status = SearchForwardIndicesForOneEmm();

      // If we invoked |config_cache_|::Fetch(), then control has passed
      // out of this class for now. It will resume from
      // OnForwardIndexFetched().
      if (status == ForwardIndexSearchStatus::kWillResumeOnFetchCompletion) {
        break;
      }
    }
  }

  // Called by one of
  // *  SplitMakeAndModel() or
  // *  OnReverseIndexFetched().
  // Continues a prior call to SplitMakeAndModel().
  //
  // Looks for |effective_make_and_model| in the reverse index named by
  // |metadata_name|, and tries to invoke |cb| with the split make and
  // model.
  void OnReverseIndexAvailable(base::StringPiece metadata_name,
                               base::StringPiece effective_make_and_model,
                               PpdProvider::ReverseLookupCallback cb) {
    const auto& parsed_reverse_index =
        cached_reverse_indices_.at(metadata_name);

    // This is likely a bug: we'd expect that this reverse index
    // contains the decomposition for |effective_make_and_model|.
    if (!parsed_reverse_index.value.contains(effective_make_and_model)) {
      base::SequencedTaskRunnerHandle::Get()->PostTask(
          FROM_HERE,
          base::BindOnce(std::move(cb),
                         PpdProvider::CallbackResultCode::INTERNAL_ERROR, "",
                         ""));
      return;
    }

    const ReverseIndexLeaf& leaf =
        parsed_reverse_index.value.at(effective_make_and_model);

    base::SequencedTaskRunnerHandle::Get()->PostTask(
        FROM_HERE,
        base::BindOnce(std::move(cb), PpdProvider::CallbackResultCode::SUCCESS,
                       leaf.manufacturer, leaf.model));
  }

  // Called by |config_cache_|.Fetch().
  // Continues a prior call to SplitMakeAndModel().
  //
  // Parses and updates our cached map of reverse indices if |result|
  // indicates a successful fetch. Calls |cb| accordingly.
  void OnReverseIndexFetched(std::string effective_make_and_model,
                             PpdProvider::ReverseLookupCallback cb,
                             const PrinterConfigCache::FetchResult& result) {
    DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

    if (!result.succeeded) {
      base::SequencedTaskRunnerHandle::Get()->PostTask(
          FROM_HERE,
          base::BindOnce(std::move(cb),
                         PpdProvider::CallbackResultCode::SERVER_ERROR, "",
                         ""));
      return;
    }

    const auto parsed = ParseReverseIndex(result.contents);
    if (!parsed.has_value()) {
      base::SequencedTaskRunnerHandle::Get()->PostTask(
          FROM_HERE,
          base::BindOnce(std::move(cb),
                         PpdProvider::CallbackResultCode::INTERNAL_ERROR, "",
                         ""));
      return;
    }

    ParsedMetadataWithTimestamp<ParsedReverseIndex> value = {clock_->Now(),
                                                             parsed.value()};
    cached_reverse_indices_.insert_or_assign(result.key, value);
    OnReverseIndexAvailable(result.key, effective_make_and_model,
                            std::move(cb));
  }

  const std::string browser_locale_;
  const base::Clock* clock_;

  // The closest match to |browser_locale_| for which the serving root
  // claims to serve metadata.
  std::string metadata_locale_;

  std::unique_ptr<PrinterConfigCache> config_cache_;

  CachedParsedMetadataMap<ParsedManufacturers> cached_manufacturers_;
  CachedParsedMetadataMap<ParsedPrinters> cached_printers_;
  CachedParsedMetadataMap<ParsedIndex> cached_forward_indices_;
  CachedParsedMetadataMap<ParsedReverseIndex> cached_reverse_indices_;

  // Processing queue for FindAllEmmsAvailableInIndex().
  ForwardIndexSearchQueue forward_index_search_queue_;

  SEQUENCE_CHECKER(sequence_checker_);

  // Dispenses weak pointers to the |config_cache_|. This is necessary
  // because |this| could be deleted while the |config_cache_| is
  // processing something off-sequence.
  base::WeakPtrFactory<PpdMetadataManagerImpl> weak_factory_;
};

}  // namespace

// static
std::unique_ptr<PpdMetadataManager> PpdMetadataManager::Create(
    base::StringPiece browser_locale,
    base::Clock* clock,
    std::unique_ptr<PrinterConfigCache> config_cache) {
  return std::make_unique<PpdMetadataManagerImpl>(browser_locale, clock,
                                                  std::move(config_cache));
}

}  // namespace chromeos
