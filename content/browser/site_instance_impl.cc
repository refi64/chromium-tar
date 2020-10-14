// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "content/browser/site_instance_impl.h"

#include <string>
#include <tuple>

#include "base/command_line.h"
#include "base/debug/crash_logging.h"
#include "base/debug/dump_without_crashing.h"
#include "base/lazy_instance.h"
#include "base/macros.h"
#include "content/browser/bad_message.h"
#include "content/browser/browsing_instance.h"
#include "content/browser/child_process_security_policy_impl.h"
#include "content/browser/isolated_origin_util.h"
#include "content/browser/isolation_context.h"
#include "content/browser/renderer_host/agent_scheduling_group_host.h"
#include "content/browser/renderer_host/render_process_host_impl.h"
#include "content/browser/storage_partition_impl.h"
#include "content/browser/webui/url_data_manager_backend.h"
#include "content/public/browser/browser_or_resource_context.h"
#include "content/public/browser/content_browser_client.h"
#include "content/public/browser/render_process_host_factory.h"
#include "content/public/browser/site_isolation_policy.h"
#include "content/public/browser/web_ui_controller_factory.h"
#include "content/public/common/content_client.h"
#include "content/public/common/content_features.h"
#include "content/public/common/content_switches.h"
#include "content/public/common/url_constants.h"
#include "content/public/common/url_utils.h"
#include "net/base/registry_controlled_domains/registry_controlled_domain.h"
#include "url/origin.h"

namespace content {

namespace {

GURL SchemeAndHostToSite(const std::string& scheme, const std::string& host) {
  return GURL(scheme + url::kStandardSchemeSeparator + host);
}

}  // namespace

int32_t SiteInstanceImpl::next_site_instance_id_ = 1;

// static
const GURL& SiteInstanceImpl::GetDefaultSiteURL() {
  struct DefaultSiteURL {
    const GURL url = GURL("http://unisolated.invalid");
  };
  static base::LazyInstance<DefaultSiteURL>::Leaky default_site_url =
      LAZY_INSTANCE_INITIALIZER;

  return default_site_url.Get().url;
}

// static
SiteInfo SiteInfo::CreateForErrorPage() {
  return SiteInfo(GURL(content::kUnreachableWebDataURL),
                  GURL(content::kUnreachableWebDataURL),
                  false /* is_origin_keyed */);
}

SiteInfo::SiteInfo(const GURL& site_url,
                   const GURL& process_lock_url,
                   bool is_origin_keyed)
    : site_url_(site_url),
      process_lock_url_(process_lock_url),
      is_origin_keyed_(is_origin_keyed) {}

// static
auto SiteInfo::MakeTie(const SiteInfo& site_info) {
  return std::tie(site_info.site_url_.possibly_invalid_spec(),
                  site_info.process_lock_url_.possibly_invalid_spec(),
                  site_info.is_origin_keyed_);
}

bool SiteInfo::operator==(const SiteInfo& other) const {
  return MakeTie(*this) == MakeTie(other);
}

bool SiteInfo::operator!=(const SiteInfo& other) const {
  return MakeTie(*this) != MakeTie(other);
}

bool SiteInfo::operator<(const SiteInfo& other) const {
  return MakeTie(*this) < MakeTie(other);
}

std::string SiteInfo::GetDebugString() const {
  // TODO(wjmaclean): At some point we should consider adding output about
  // origin- vs. site-keying.
  return site_url_.possibly_invalid_spec();
}

std::ostream& operator<<(std::ostream& out, const SiteInfo& site_info) {
  return out << site_info.GetDebugString();
}

SiteInstanceImpl::SiteInstanceImpl(BrowsingInstance* browsing_instance)
    : id_(next_site_instance_id_++),
      active_frame_count_(0),
      browsing_instance_(browsing_instance),
      process_(nullptr),
      agent_scheduling_group_(nullptr),
      can_associate_with_spare_process_(true),
      has_site_(false),
      process_reuse_policy_(ProcessReusePolicy::DEFAULT),
      is_for_service_worker_(false),
      is_guest_(false) {
  DCHECK(browsing_instance);
}

SiteInstanceImpl::~SiteInstanceImpl() {
  GetContentClient()->browser()->SiteInstanceDeleting(this);

  if (process_) {
    process_->RemoveObserver(this);

    // Ensure the RenderProcessHost gets deleted if this SiteInstance created a
    // process which was never used by any listeners.
    process_->Cleanup();
  }

  // Now that no one is referencing us, we can safely remove ourselves from
  // the BrowsingInstance.  Any future visits to a page from this site
  // (within the same BrowsingInstance) can safely create a new SiteInstance.
  if (has_site_)
    browsing_instance_->UnregisterSiteInstance(this);
}

// static
scoped_refptr<SiteInstanceImpl> SiteInstanceImpl::Create(
    BrowserContext* browser_context) {
  DCHECK(browser_context);
  return base::WrapRefCounted(
      new SiteInstanceImpl(new BrowsingInstance(browser_context)));
}

// static
scoped_refptr<SiteInstanceImpl> SiteInstanceImpl::CreateForURL(
    BrowserContext* browser_context,
    const GURL& url) {
  DCHECK(browser_context);
  // This will create a new SiteInstance and BrowsingInstance.
  scoped_refptr<BrowsingInstance> instance(
      new BrowsingInstance(browser_context));

  // Note: The |allow_default_instance| value used here MUST match the value
  // used in DoesSiteForURLMatch().
  return instance->GetSiteInstanceForURL(url,
                                         true /* allow_default_instance */);
}

// static
scoped_refptr<SiteInstanceImpl> SiteInstanceImpl::CreateForServiceWorker(
    BrowserContext* browser_context,
    const GURL& url,
    bool can_reuse_process,
    bool is_guest) {
  scoped_refptr<SiteInstanceImpl> site_instance;

  if (is_guest) {
    site_instance = CreateForGuest(browser_context, url);
  } else {
    // This will create a new SiteInstance and BrowsingInstance.
    scoped_refptr<BrowsingInstance> instance(
        new BrowsingInstance(browser_context));

    // We do NOT want to allow the default site instance here because workers
    // need to be kept separate from other sites.
    site_instance = instance->GetSiteInstanceForURL(
        url, /* allow_default_instance */ false);
  }
  site_instance->is_for_service_worker_ = true;

  // Attempt to reuse a renderer process if possible. Note that in the
  // <webview> case, process reuse isn't currently supported and a new
  // process will always be created (https://crbug.com/752667).
  DCHECK(site_instance->process_reuse_policy() ==
             SiteInstanceImpl::ProcessReusePolicy::DEFAULT ||
         site_instance->process_reuse_policy() ==
             SiteInstanceImpl::ProcessReusePolicy::PROCESS_PER_SITE);
  if (can_reuse_process) {
    site_instance->set_process_reuse_policy(
        SiteInstanceImpl::ProcessReusePolicy::REUSE_PENDING_OR_COMMITTED_SITE);
  }
  return site_instance;
}

// static
scoped_refptr<SiteInstanceImpl> SiteInstanceImpl::CreateForGuest(
    content::BrowserContext* browser_context,
    const GURL& guest_site_url) {
  DCHECK(browser_context);
  DCHECK_NE(guest_site_url, GetDefaultSiteURL());
  scoped_refptr<SiteInstanceImpl> site_instance = base::WrapRefCounted(
      new SiteInstanceImpl(new BrowsingInstance(browser_context)));

  site_instance->is_guest_ = true;

  // Setting site and lock directly without the site URL conversions we
  // do for user provided URLs. Callers expect GetSiteURL() to return the
  // value they provide in |guest_site_url|.
  site_instance->SetSiteInfoInternal(
      SiteInfo(guest_site_url, guest_site_url, false /* is_origin_keyed */));

  return site_instance;
}

// static
scoped_refptr<SiteInstanceImpl>
SiteInstanceImpl::CreateReusableInstanceForTesting(
    BrowserContext* browser_context,
    const GURL& url) {
  DCHECK(browser_context);
  // This will create a new SiteInstance and BrowsingInstance.
  scoped_refptr<BrowsingInstance> instance(
      new BrowsingInstance(browser_context));
  auto site_instance =
      instance->GetSiteInstanceForURL(url,
                                      /* allow_default_instance */ false);
  site_instance->set_process_reuse_policy(
      SiteInstanceImpl::ProcessReusePolicy::REUSE_PENDING_OR_COMMITTED_SITE);
  return site_instance;
}

// static
bool SiteInstanceImpl::ShouldAssignSiteForURL(const GURL& url) {
  // about:blank should not "use up" a new SiteInstance.  The SiteInstance can
  // still be used for a normal web site.
  if (url.IsAboutBlank())
    return false;

  // The embedder will then have the opportunity to determine if the URL
  // should "use up" the SiteInstance.
  return GetContentClient()->browser()->ShouldAssignSiteForURL(url);
}

int32_t SiteInstanceImpl::GetId() {
  return id_;
}

int32_t SiteInstanceImpl::GetBrowsingInstanceId() {
  // This is being vended out as an opaque ID, and it is always defined for
  // a BrowsingInstance affiliated IsolationContext, so it's safe to call
  // "GetUnsafeValue" and expose the inner value directly.
  return browsing_instance_->isolation_context()
      .browsing_instance_id()
      .GetUnsafeValue();
}

const IsolationContext& SiteInstanceImpl::GetIsolationContext() {
  return browsing_instance_->isolation_context();
}

RenderProcessHost* SiteInstanceImpl::GetDefaultProcessIfUsable() {
  if (!base::FeatureList::IsEnabled(
          features::kProcessSharingWithStrictSiteInstances)) {
    return nullptr;
  }
  if (RequiresDedicatedProcess())
    return nullptr;
  return browsing_instance_->default_process();
}

bool SiteInstanceImpl::IsDefaultSiteInstance() const {
  return browsing_instance_->IsDefaultSiteInstance(this);
}

bool SiteInstanceImpl::IsSiteInDefaultSiteInstance(const GURL& site_url) const {
  return browsing_instance_->IsSiteInDefaultSiteInstance(site_url);
}

void SiteInstanceImpl::MaybeSetBrowsingInstanceDefaultProcess() {
  if (!base::FeatureList::IsEnabled(
          features::kProcessSharingWithStrictSiteInstances)) {
    return;
  }
  // Wait until this SiteInstance both has a site and a process
  // assigned, so that we can be sure that RequiresDedicatedProcess()
  // is accurate and we actually have a process to set.
  if (!process_ || !has_site_ || RequiresDedicatedProcess())
    return;
  if (browsing_instance_->default_process()) {
    DCHECK_EQ(process_, browsing_instance_->default_process());
    return;
  }
  browsing_instance_->SetDefaultProcess(process_);
}

// static
BrowsingInstanceId SiteInstanceImpl::NextBrowsingInstanceId() {
  return BrowsingInstance::NextBrowsingInstanceId();
}

bool SiteInstanceImpl::HasProcess() {
  if (process_ != nullptr)
    return true;

  // If we would use process-per-site for this site, also check if there is an
  // existing process that we would use if GetProcess() were called.
  BrowserContext* browser_context = browsing_instance_->GetBrowserContext();
  if (has_site_ &&
      RenderProcessHostImpl::ShouldUseProcessPerSite(browser_context,
                                                     site_info_) &&
      RenderProcessHostImpl::GetSoleProcessHostForSite(GetIsolationContext(),
                                                       site_info_, IsGuest())) {
    return true;
  }

  return false;
}

RenderProcessHost* SiteInstanceImpl::GetProcess() {
  // TODO(erikkay) It would be nice to ensure that the renderer type had been
  // properly set before we get here.  The default tab creation case winds up
  // with no site set at this point, so it will default to TYPE_NORMAL.  This
  // may not be correct, so we'll wind up potentially creating a process that
  // we then throw away, or worse sharing a process with the wrong process type.
  // See crbug.com/43448.

  // Create a new process if ours went away or was reused.
  if (!process_) {
    BrowserContext* browser_context = browsing_instance_->GetBrowserContext();

    // Check if the ProcessReusePolicy should be updated.
    bool should_use_process_per_site =
        has_site_ && RenderProcessHostImpl::ShouldUseProcessPerSite(
                         browser_context, site_info_);
    if (should_use_process_per_site) {
      process_reuse_policy_ = ProcessReusePolicy::PROCESS_PER_SITE;
    } else if (process_reuse_policy_ == ProcessReusePolicy::PROCESS_PER_SITE) {
      process_reuse_policy_ = ProcessReusePolicy::DEFAULT;
    }

    SetProcessInternal(
        RenderProcessHostImpl::GetProcessHostForSiteInstance(this));
  }
  DCHECK(process_);

  return process_;
}

AgentSchedulingGroupHost& SiteInstanceImpl::GetAgentSchedulingGroup() {
  if (!agent_scheduling_group_) {
    // If an AgentSchedulingGroup has not yet been assigned, we need to have it
    // assigned (along with a RenderProcessHost). To preserve the invariant that
    // |process_| and |agent_scheduling_group_| are always changed together, we
    // call GetProcess(), and assume that it will set both members.
    GetProcess();
  }

  DCHECK(agent_scheduling_group_);
  DCHECK_EQ(agent_scheduling_group_->GetProcess(), process_);

  return *agent_scheduling_group_;
}

void SiteInstanceImpl::ReuseCurrentProcessIfPossible(
    RenderProcessHost* current_process) {
  DCHECK(!IsGuest());
  if (HasProcess())
    return;

  // We should not reuse the current process if the destination uses
  // process-per-site. Note that this includes the case where the process for
  // the site is not there yet (so we're going to create a new process).
  // Note also that this does not apply for the reverse case: if the current
  // process is used for a process-per-site site, it is ok to reuse this for the
  // new page (regardless of the site).
  if (HasSite() && RenderProcessHostImpl::ShouldUseProcessPerSite(
                       browsing_instance_->GetBrowserContext(), site_info_)) {
    return;
  }

  // Do not reuse the process if it's not suitable for this SiteInstance. For
  // example, this won't allow reusing a process if it's locked to a site that's
  // different from this SiteInstance's site.
  if (!current_process->MayReuseHost() ||
      !RenderProcessHostImpl::IsSuitableHost(
          current_process, GetIsolationContext(), site_info_, IsGuest())) {
    return;
  }

  // TODO(crbug.com/1055779 ): Don't try to reuse process if either of the
  // SiteInstances are cross-origin isolated (uses COOP/COEP).
  SetProcessInternal(current_process);
}

void SiteInstanceImpl::SetProcessInternal(RenderProcessHost* process) {
  //  It is never safe to change |process_| without going through
  //  RenderProcessHostDestroyed first to set it to null. Otherwise, same-site
  //  frames will end up in different processes and everything will get
  //  confused.
  CHECK(!process_);
  CHECK(process);
  process_ = process;
  process_->AddObserver(this);
  DCHECK(!agent_scheduling_group_);
  agent_scheduling_group_ = AgentSchedulingGroupHost::Get(*this, *process_);

  MaybeSetBrowsingInstanceDefaultProcess();

  // If we are using process-per-site, we need to register this process
  // for the current site so that we can find it again.  (If no site is set
  // at this time, we will register it in SetSite().)
  if (process_reuse_policy_ == ProcessReusePolicy::PROCESS_PER_SITE &&
      has_site_) {
    RenderProcessHostImpl::RegisterSoleProcessHostForSite(process_, this);
  }

  TRACE_EVENT2("navigation", "SiteInstanceImpl::SetProcessInternal", "site id",
               id_, "process id", process_->GetID());
  GetContentClient()->browser()->SiteInstanceGotProcess(this);

  if (has_site_)
    LockProcessIfNeeded();
}

bool SiteInstanceImpl::CanAssociateWithSpareProcess() {
  return can_associate_with_spare_process_;
}

void SiteInstanceImpl::PreventAssociationWithSpareProcess() {
  can_associate_with_spare_process_ = false;
}

void SiteInstanceImpl::SetSite(const GURL& url) {
  // TODO(creis): Consider calling ShouldAssignSiteForURL internally, rather
  // than before multiple call sites.  See https://crbug.com/949220.
  TRACE_EVENT2("navigation", "SiteInstanceImpl::SetSite",
               "site id", id_, "url", url.possibly_invalid_spec());
  // A SiteInstance's site should not change.
  // TODO(creis): When following links or script navigations, we can currently
  // render pages from other sites in this SiteInstance.  This will eventually
  // be fixed, but until then, we should still not set the site of a
  // SiteInstance more than once.
  DCHECK(!has_site_);

  original_url_ = url;
  // Convert |url| into an appropriate SiteInfo that can be passed to
  // SetSiteInfoInternal(). We must do this transformation for any arbitrary
  // URL we get from a user, a navigation, or script.
  SetSiteInfoInternal(browsing_instance_->GetSiteInfoForURL(
      url, /* allow_default_instance */ false));
}

void SiteInstanceImpl::SetSiteInfoInternal(const SiteInfo& site_info) {
  // TODO(acolwell): Add logic to validate |site_url| and |lock_url| are valid.
  DCHECK(!has_site_);

  // Remember that this SiteInstance has been used to load a URL, even if the
  // URL is invalid.
  has_site_ = true;
  site_info_ = site_info;

  if (site_info_.is_origin_keyed()) {
    // Track this origin's isolation in the current BrowsingInstance.  This is
    // needed to consistently isolate future navigations to this origin in this
    // BrowsingInstance, even if its opt-in status changes later.
    ChildProcessSecurityPolicyImpl* policy =
        ChildProcessSecurityPolicyImpl::GetInstance();
    url::Origin site_origin(url::Origin::Create(site_info_.site_url()));
    policy->AddOptInIsolatedOriginForBrowsingInstance(
        browsing_instance_->isolation_context(), site_origin);
  }

  // Now that we have a site, register it with the BrowsingInstance.  This
  // ensures that we won't create another SiteInstance for this site within
  // the same BrowsingInstance, because all same-site pages within a
  // BrowsingInstance can script each other.
  browsing_instance_->RegisterSiteInstance(this);

  // Update the process reuse policy based on the site.
  BrowserContext* browser_context = browsing_instance_->GetBrowserContext();
  bool should_use_process_per_site =
      RenderProcessHostImpl::ShouldUseProcessPerSite(browser_context,
                                                     site_info_);
  if (should_use_process_per_site) {
    process_reuse_policy_ = ProcessReusePolicy::PROCESS_PER_SITE;
  }

  if (process_) {
    LockProcessIfNeeded();

    // Ensure the process is registered for this site if necessary.
    if (should_use_process_per_site)
      RenderProcessHostImpl::RegisterSoleProcessHostForSite(process_, this);

    MaybeSetBrowsingInstanceDefaultProcess();
  }
}

void SiteInstanceImpl::ConvertToDefaultOrSetSite(const GURL& url) {
  DCHECK(!has_site_);

  if (browsing_instance_->TrySettingDefaultSiteInstance(this, url))
    return;

  SetSite(url);
}

const GURL& SiteInstanceImpl::GetSiteURL() {
  return site_info_.site_url();
}

const SiteInfo& SiteInstanceImpl::GetSiteInfo() {
  return site_info_;
}

const ProcessLock SiteInstanceImpl::GetProcessLock() const {
  return ProcessLock(site_info_);
}

bool SiteInstanceImpl::HasSite() const {
  return has_site_;
}

bool SiteInstanceImpl::HasRelatedSiteInstance(const SiteInfo& site_info) {
  return browsing_instance_->HasSiteInstance(site_info);
}

scoped_refptr<SiteInstance> SiteInstanceImpl::GetRelatedSiteInstance(
    const GURL& url) {
  return browsing_instance_->GetSiteInstanceForURL(
      url, /* allow_default_instance */ true);
}

bool SiteInstanceImpl::IsRelatedSiteInstance(const SiteInstance* instance) {
  return browsing_instance_.get() == static_cast<const SiteInstanceImpl*>(
                                         instance)->browsing_instance_.get();
}

size_t SiteInstanceImpl::GetRelatedActiveContentsCount() {
  return browsing_instance_->active_contents_count();
}

bool SiteInstanceImpl::IsSuitableForURL(const GURL& url) {
  DCHECK_CURRENTLY_ON(BrowserThread::UI);
  // If the URL to navigate to can be associated with any site instance,
  // we want to keep it in the same process.
  if (IsRendererDebugURL(url))
    return true;

  // Any process can host an about:blank URL, except the one used for error
  // pages, which should not commit successful navigations.  This check avoids a
  // process transfer for browser-initiated navigations to about:blank in a
  // dedicated process; without it, IsSuitableHost would consider this process
  // unsuitable for about:blank when it compares process locks.
  // Renderer-initiated navigations will handle about:blank navigations
  // elsewhere and leave them in the source SiteInstance, along with
  // about:srcdoc and data:.
  if (url.IsAboutBlank() && site_info_ != SiteInfo::CreateForErrorPage())
    return true;

  // If the site URL is an extension (e.g., for hosted apps or WebUI) but the
  // process is not (or vice versa), make sure we notice and fix it.

  // Note: This call must return information that is identical to what
  // would be reported in the SiteInstance returned by
  // GetRelatedSiteInstance(url).
  SiteInfo site_info = browsing_instance_->GetSiteInfoForURL(
      url, /* allow_default_instance */ true);

  // If this is a default SiteInstance and the BrowsingInstance gives us a
  // non-default site URL even when we explicitly allow the default SiteInstance
  // to be considered, then |url| does not belong in the same process as this
  // SiteInstance. This can happen when the
  // kProcessSharingWithDefaultSiteInstances feature is not enabled and the
  // site URL is explicitly set on a SiteInstance for a URL that would normally
  // be directed to the default SiteInstance (e.g. a site not requiring a
  // dedicated process). This situation typically happens when the top-level
  // frame is a site that should be in the default SiteInstance and the
  // SiteInstance associated with that frame is initially a SiteInstance with
  // no site URL set.
  if (IsDefaultSiteInstance() && site_info != site_info_)
    return false;

  // Note that HasProcess() may return true if process_ is null, in
  // process-per-site cases where there's an existing process available.
  // We want to use such a process in the IsSuitableHost check, so we
  // may end up assigning process_ in the GetProcess() call below.
  if (!HasProcess()) {
    // If there is no process or site, then this is a new SiteInstance that can
    // be used for anything.
    if (!HasSite())
      return true;

    // If there is no process but there is a site, then the process must have
    // been discarded after we navigated away.  If the site URLs match, then it
    // is safe to use this SiteInstance.
    if (site_info_ == site_info)
      return true;

    // If the site URLs do not match, but neither this SiteInstance nor the
    // destination site_url require dedicated processes, then it is safe to use
    // this SiteInstance.
    if (!RequiresDedicatedProcess() &&
        !DoesSiteURLRequireDedicatedProcess(GetIsolationContext(),
                                            site_info.site_url())) {
      return true;
    }

    // Otherwise, there's no process, the site URLs don't match, and at least
    // one of them requires a dedicated process, so it is not safe to use this
    // SiteInstance.
    return false;
  }

  return RenderProcessHostImpl::IsSuitableHost(
      GetProcess(), GetIsolationContext(), site_info, IsGuest());
}

bool SiteInstanceImpl::RequiresDedicatedProcess() {
  DCHECK_CURRENTLY_ON(BrowserThread::UI);
  if (!has_site_)
    return false;

  return DoesSiteURLRequireDedicatedProcess(GetIsolationContext(),
                                            site_info_.site_url());
}

void SiteInstanceImpl::IncrementActiveFrameCount() {
  active_frame_count_++;
}

void SiteInstanceImpl::DecrementActiveFrameCount() {
  if (--active_frame_count_ == 0) {
    for (auto& observer : observers_)
      observer.ActiveFrameCountIsZero(this);
  }
}

void SiteInstanceImpl::IncrementRelatedActiveContentsCount() {
  browsing_instance_->increment_active_contents_count();
}

void SiteInstanceImpl::DecrementRelatedActiveContentsCount() {
  browsing_instance_->decrement_active_contents_count();
}

void SiteInstanceImpl::AddObserver(Observer* observer) {
  observers_.AddObserver(observer);
}

void SiteInstanceImpl::RemoveObserver(Observer* observer) {
  observers_.RemoveObserver(observer);
}

BrowserContext* SiteInstanceImpl::GetBrowserContext() {
  return browsing_instance_->GetBrowserContext();
}

// static
scoped_refptr<SiteInstance> SiteInstance::Create(
    BrowserContext* browser_context) {
  DCHECK(browser_context);
  return SiteInstanceImpl::Create(browser_context);
}

// static
scoped_refptr<SiteInstance> SiteInstance::CreateForURL(
    BrowserContext* browser_context,
    const GURL& url) {
  DCHECK(browser_context);
  return SiteInstanceImpl::CreateForURL(browser_context, url);
}

// static
scoped_refptr<SiteInstance> SiteInstance::CreateForGuest(
    content::BrowserContext* browser_context,
    const GURL& guest_site_url) {
  DCHECK(browser_context);
  return SiteInstanceImpl::CreateForGuest(browser_context, guest_site_url);
}

// static
bool SiteInstance::ShouldAssignSiteForURL(const GURL& url) {
  return SiteInstanceImpl::ShouldAssignSiteForURL(url);
}

bool SiteInstanceImpl::IsSameSiteWithURL(const GURL& url) {
  if (IsDefaultSiteInstance()) {
    // about:blank URLs should always be considered same site just like they are
    // in IsSameSite().
    if (url.IsAboutBlank())
      return true;

    // Consider |url| the same site if it could be handled by the
    // default SiteInstance and we don't already have a SiteInstance for
    // this URL.
    // TODO(acolwell): Remove HasSiteInstance() call once we have a way to
    // prevent SiteInstances with no site URL from being used for URLs
    // that should be routed to the default SiteInstance.
    DCHECK_EQ(site_info_.site_url(), GetDefaultSiteURL());
    return site_info_.site_url() ==
               GetSiteForURLInternal(GetIsolationContext(), url,
                                     true /* should_use_effective_urls */,
                                     true /* allow_default_site_url */) &&
           !browsing_instance_->HasSiteInstance(
               ComputeSiteInfo(GetIsolationContext(), url));
  }

  return SiteInstanceImpl::IsSameSite(GetIsolationContext(),
                                      site_info_.site_url(), url,
                                      true /* should_compare_effective_urls */);
}

bool SiteInstanceImpl::IsGuest() {
  return is_guest_;
}

std::string SiteInstanceImpl::GetPartitionDomain(
    StoragePartitionImpl* storage_partition) {
  auto storage_partition_config =
      GetContentClient()->browser()->GetStoragePartitionConfigForSite(
          GetBrowserContext(), GetSiteURL());

  // The DCHECK here is to allow the trybots to detect any attempt to introduce
  // new code that violates this assumption.
  DCHECK_EQ(storage_partition->GetPartitionDomain(),
            storage_partition_config.partition_domain());

  if (storage_partition->GetPartitionDomain() !=
      storage_partition_config.partition_domain()) {
    // Trigger crash logging if we encounter a case that violates our
    // assumptions.
    static auto* storage_partition_domain_key =
        base::debug::AllocateCrashKeyString("storage_partition_domain",
                                            base::debug::CrashKeySize::Size256);
    static auto* storage_partition_config_domain_key =
        base::debug::AllocateCrashKeyString(
            "storage_partition_config_domain_key",
            base::debug::CrashKeySize::Size256);
    base::debug::SetCrashKeyString(storage_partition_domain_key,
                                   storage_partition->GetPartitionDomain());
    base::debug::SetCrashKeyString(storage_partition_config_domain_key,
                                   storage_partition_config.partition_domain());

    base::debug::DumpWithoutCrashing();

    // Return the value from the config to preserve legacy behavior until we
    // can land a fix.
    return storage_partition_config.partition_domain();
  }
  return storage_partition->GetPartitionDomain();
}

bool SiteInstanceImpl::IsOriginalUrlSameSite(
    const GURL& dest_url,
    bool should_compare_effective_urls) {
  if (IsDefaultSiteInstance())
    return IsSameSiteWithURL(dest_url);

  return IsSameSite(GetIsolationContext(), original_url_, dest_url,
                    should_compare_effective_urls);
}

// static
bool SiteInstanceImpl::IsSameSite(const IsolationContext& isolation_context,
                                  const GURL& real_src_url,
                                  const GURL& real_dest_url,
                                  bool should_compare_effective_urls) {
  DCHECK_CURRENTLY_ON(BrowserThread::UI);
  BrowserContext* browser_context =
      isolation_context.browser_or_resource_context().ToBrowserContext();
  DCHECK(browser_context);
  DCHECK_NE(real_src_url, GetDefaultSiteURL());

  GURL src_url =
      should_compare_effective_urls
          ? SiteInstanceImpl::GetEffectiveURL(browser_context, real_src_url)
          : real_src_url;
  GURL dest_url =
      should_compare_effective_urls
          ? SiteInstanceImpl::GetEffectiveURL(browser_context, real_dest_url)
          : real_dest_url;

  // We infer web site boundaries based on the registered domain name of the
  // top-level page and the scheme.  We do not pay attention to the port if
  // one is present, because pages served from different ports can still
  // access each other if they change their document.domain variable.

  // Some special URLs will match the site instance of any other URL. This is
  // done before checking both of them for validity, since we want these URLs
  // to have the same site instance as even an invalid one.
  if (IsRendererDebugURL(src_url) || IsRendererDebugURL(dest_url))
    return true;

  // If either URL is invalid, they aren't part of the same site.
  if (!src_url.is_valid() || !dest_url.is_valid())
    return false;

  // If the destination url is just a blank page, we treat them as part of the
  // same site.
  if (dest_url.IsAboutBlank())
    return true;

  // If the source and destination URLs are equal excluding the hash, they have
  // the same site.  This matters for file URLs, where SameDomainOrHost() would
  // otherwise return false below.
  if (src_url.EqualsIgnoringRef(dest_url))
    return true;

  url::Origin src_origin = url::Origin::Create(src_url);
  url::Origin dest_origin = url::Origin::Create(dest_url);

  // If the schemes differ, they aren't part of the same site.
  if (src_origin.scheme() != dest_origin.scheme())
    return false;

  if (SiteIsolationPolicy::IsStrictOriginIsolationEnabled())
    return src_origin == dest_origin;

  if (!net::registry_controlled_domains::SameDomainOrHost(
          src_origin, dest_origin,
          net::registry_controlled_domains::INCLUDE_PRIVATE_REGISTRIES)) {
    return false;
  }

  // If the sites are the same, check isolated origins.  If either URL matches
  // an isolated origin, compare origins rather than sites.  As an optimization
  // to avoid unneeded isolated origin lookups, shortcut this check if the two
  // origins are the same.
  if (src_origin == dest_origin)
    return true;
  auto* policy = ChildProcessSecurityPolicyImpl::GetInstance();
  url::Origin src_isolated_origin;
  url::Origin dest_isolated_origin;
  bool src_origin_is_isolated = policy->GetMatchingIsolatedOrigin(
      isolation_context, src_origin, &src_isolated_origin);
  bool dest_origin_is_isolated = policy->GetMatchingIsolatedOrigin(
      isolation_context, dest_origin, &dest_isolated_origin);
  if (src_origin_is_isolated || dest_origin_is_isolated) {
    // Compare most specific matching origins to ensure that a subdomain of an
    // isolated origin (e.g., https://subdomain.isolated.foo.com) also matches
    // the isolated origin's site URL (e.g., https://isolated.foo.com).
    return src_isolated_origin == dest_isolated_origin;
  }

  return true;
}

bool SiteInstanceImpl::DoesSiteInfoForURLMatch(const GURL& url) {
  auto* policy = ChildProcessSecurityPolicyImpl::GetInstance();
  bool is_origin_keyed = policy->ShouldOriginGetOptInIsolation(
      GetIsolationContext(), url::Origin::Create(url));

  // Note: The |allow_default_site_url| value used here MUST match the value
  // used in CreateForURL().  This is why we can't use ComputeSiteInfo() or
  // even DetermineProcessLockURL() here, which do not allow the default site
  // URL.
  return site_info_ ==
         SiteInfo(GetSiteForURLInternal(GetIsolationContext(), url,
                                        true /* should_use_effective_urls */,
                                        true /* allow_default_site_url */),
                  GetSiteForURLInternal(GetIsolationContext(), url,
                                        false /* should_use_effective_urls */,
                                        true /* allow_default_site_url */),
                  is_origin_keyed);
}

void SiteInstanceImpl::PreventOptInOriginIsolation(
    const url::Origin& previously_visited_origin) {
  auto* policy = ChildProcessSecurityPolicyImpl::GetInstance();
  policy->AddNonIsolatedOriginIfNeeded(GetIsolationContext(),
                                       previously_visited_origin,
                                       true /* is_global_walk */);
}

// static
GURL SiteInstance::GetSiteForURL(BrowserContext* browser_context,
                                 const GURL& url) {
  DCHECK_CURRENTLY_ON(BrowserThread::UI);
  DCHECK(browser_context);

  // By default, GetSiteForURL will resolve |url| to an effective URL
  // before computing its site.
  //
  // TODO(alexmos): Callers inside content/ should already be using the
  // internal SiteInstanceImpl version and providing a proper IsolationContext.
  // For callers outside content/, plumb the applicable IsolationContext here,
  // where needed.  Eventually, GetSiteForURL should always require an
  // IsolationContext to be passed in, and this implementation should just
  // become SiteInstanceImpl::GetSiteForURL.
  return SiteInstanceImpl::GetSiteForURL(IsolationContext(browser_context),
                                         url);
}

SiteInfo SiteInstanceImpl::ComputeSiteInfo(
    const IsolationContext& isolation_context,
    const GURL& url) {
  // The call to GetSiteForURL() below is only allowed on the UI thread, due to
  // its possible use of effective urls.
  DCHECK_CURRENTLY_ON(BrowserThread::UI);
  // This function will expand as more information is included in SiteInfo.
  bool is_origin_keyed = ChildProcessSecurityPolicyImpl::GetInstance()
                             ->ShouldOriginGetOptInIsolation(
                                 isolation_context, url::Origin::Create(url));

  return SiteInfo(GetSiteForURL(isolation_context, url),
                  DetermineProcessLockURL(isolation_context, url),
                  is_origin_keyed);
}

// static
ProcessLock SiteInstanceImpl::DetermineProcessLock(
    const IsolationContext& isolation_context,
    const GURL& url) {
  if (BrowserThread::CurrentlyOn(BrowserThread::UI))
    return ProcessLock(ComputeSiteInfo(isolation_context, url));

  DCHECK_CURRENTLY_ON(BrowserThread::IO);
  GURL lock_url = DetermineProcessLockURL(isolation_context, url);
  bool is_origin_keyed = ChildProcessSecurityPolicyImpl::GetInstance()
                             ->ShouldOriginGetOptInIsolation(
                                 isolation_context, url::Origin::Create(url));
  // In the SiteInfo constructor below we pass the lock url as the site URL
  // also, assuming the IO-thread caller won't be looking at the site url.
  return ProcessLock(SiteInfo(lock_url, lock_url, is_origin_keyed));
}

// static
// TODO(wjmaclean): remove this if the sole call from the IO thread can be
// removed.
GURL SiteInstanceImpl::DetermineProcessLockURL(
    const IsolationContext& isolation_context,
    const GURL& url) {
  // For the process lock URL, convert |url| to a site without resolving |url|
  // to an effective URL.
  return SiteInstanceImpl::GetSiteForURLInternal(
      isolation_context, url, false /* should_use_effective_urls */,
      false /* allow_default_site_url */);
}

// static
GURL SiteInstanceImpl::GetSiteForURL(const IsolationContext& isolation_context,
                                     const GURL& real_url) {
  return GetSiteForURLInternal(isolation_context, real_url,
                               true /* should_use_effective_urls */,
                               false /* allow_default_site_url */);
}

// static
GURL SiteInstanceImpl::GetSiteForURLInternal(
    const IsolationContext& isolation_context,
    const GURL& real_url,
    bool should_use_effective_urls,
    bool allow_default_site_url) {
  // Explicitly group chrome-error: URLs based on their host component.
  // These URLs are special because we want to group them like other URLs
  // with a host even though they are considered "no access" and
  // generate an opaque origin.
  if (real_url.SchemeIs(kChromeErrorScheme))
    return SchemeAndHostToSite(real_url.scheme(), real_url.host());

  if (should_use_effective_urls)
    DCHECK_CURRENTLY_ON(BrowserThread::UI);

  GURL url = should_use_effective_urls
                 ? SiteInstanceImpl::GetEffectiveURL(
                       isolation_context.browser_or_resource_context()
                           .ToBrowserContext(),
                       real_url)
                 : real_url;
  url::Origin origin = url::Origin::Create(url);

  // If the url has a host, then determine the site.  Skip file URLs to avoid a
  // situation where site URL of file://localhost/ would mismatch Blink's origin
  // (which ignores the hostname in this case - see https://crbug.com/776160).
  GURL site_url;
  if (!origin.host().empty() && origin.scheme() != url::kFileScheme) {
    // For Strict Origin Isolation, use the full origin instead of site for all
    // HTTP/HTTPS URLs.  Note that the HTTP/HTTPS restriction guarantees that
    // we won't hit this for hosted app effective URLs (see
    // https://crbug.com/961386).
    if (SiteIsolationPolicy::IsStrictOriginIsolationEnabled() &&
        origin.GetURL().SchemeIsHTTPOrHTTPS())
      return origin.GetURL();

    site_url = GetSiteForOrigin(origin);

    // Isolated origins should use the full origin as their site URL. A
    // subdomain of an isolated origin should also use that isolated origin's
    // site URL. It is important to check |origin| (based on |url|) rather than
    // |real_url| here, since some effective URLs (such as for NTP) need to be
    // resolved prior to the isolated origin lookup.
    auto* policy = ChildProcessSecurityPolicyImpl::GetInstance();
    url::Origin isolated_origin;
    if (policy->GetMatchingIsolatedOrigin(isolation_context, origin, site_url,
                                          &isolated_origin)) {
      return isolated_origin.GetURL();
    }
  } else {
    // If there is no host but there is a scheme, return the scheme.
    // This is useful for cases like file URLs.
    if (!origin.opaque()) {
      // Prefer to use the scheme of |origin| rather than |url|, to correctly
      // cover blob:file: and filesystem:file: URIs (see also
      // https://crbug.com/697111).
      DCHECK(!origin.scheme().empty());
      site_url = GURL(origin.scheme() + ":");
    } else if (url.has_scheme()) {
      // In some cases, it is not safe to use just the scheme as a site URL, as
      // that might allow two URLs created by different sites to share a
      // process. See https://crbug.com/863623 and https://crbug.com/863069.
      //
      // TODO(alexmos,creis): This should eventually be expanded to certain
      // other schemes, such as file:.
      if (url.SchemeIsBlob() || url.scheme() == url::kDataScheme) {
        // We get here for blob URLs of form blob:null/guid.  Use the full URL
        // with the guid in that case, which isolates all blob URLs with unique
        // origins from each other.  We also get here for browser-initiated
        // navigations to data URLs, which have a unique origin and should only
        // share a process when they are identical.  Remove hash from the URL in
        // either case, since same-document navigations shouldn't use a
        // different site URL.
        if (url.has_ref()) {
          GURL::Replacements replacements;
          replacements.ClearRef();
          url = url.ReplaceComponents(replacements);
        }
        site_url = url;
      } else {
        DCHECK(!url.scheme().empty());
        site_url = GURL(url.scheme() + ":");
      }
    } else {
      // Otherwise the URL should be invalid; return an empty site.
      DCHECK(!url.is_valid()) << url;
      return GURL();
    }
  }

  // We should never get here if we're origin_keyed, otherwise we would have
  // returned after the GetMatchingIsolatedOrigin() call above.
  if (allow_default_site_url &&
      CanBePlacedInDefaultSiteInstance(isolation_context, real_url, site_url)) {
    return GetDefaultSiteURL();
  }
  return site_url;
}

// static
bool SiteInstanceImpl::CanBePlacedInDefaultSiteInstance(
    const IsolationContext& isolation_context,
    const GURL& url,
    const GURL& site_url) {
  DCHECK_CURRENTLY_ON(BrowserThread::UI);

  if (!base::FeatureList::IsEnabled(
          features::kProcessSharingWithDefaultSiteInstances)) {
    return false;
  }

  // Exclude "file://" URLs from the default SiteInstance to prevent the
  // default SiteInstance process from accumulating file access grants that
  // could be exploited by other non-isolated sites.
  if (url.SchemeIs(url::kFileScheme))
    return false;

  // Don't use the default SiteInstance when
  // kProcessSharingWithStrictSiteInstances is enabled because we want each
  // site to have its own SiteInstance object and logic elsewhere ensures
  // that those SiteInstances share a process.
  if (base::FeatureList::IsEnabled(
          features::kProcessSharingWithStrictSiteInstances)) {
    return false;
  }

  // Don't use the default SiteInstance when SiteInstance doesn't assign a
  // site URL for |url|, since in that case the SiteInstance should remain
  // unused, and a subsequent navigation should always be able to reuse it,
  // whether or not it's to a site requiring a dedicated process or to a site
  // that will use the default SiteInstance.
  if (!ShouldAssignSiteForURL(url))
    return false;

  // Allow the default SiteInstance to be used for sites that don't need to be
  // isolated in their own process.
  return !DoesSiteURLRequireDedicatedProcess(isolation_context, site_url);
}

// static
GURL SiteInstanceImpl::GetSiteForOrigin(const url::Origin& origin) {
  // Only keep the scheme and registered domain of |origin|.
  std::string domain = net::registry_controlled_domains::GetDomainAndRegistry(
      origin, net::registry_controlled_domains::INCLUDE_PRIVATE_REGISTRIES);
  return SchemeAndHostToSite(origin.scheme(),
                             domain.empty() ? origin.host() : domain);
}

// static
GURL SiteInstanceImpl::GetEffectiveURL(BrowserContext* browser_context,
                                       const GURL& url) {
  DCHECK(browser_context);
  return GetContentClient()->browser()->GetEffectiveURL(browser_context, url);
}

// static
bool SiteInstanceImpl::HasEffectiveURL(BrowserContext* browser_context,
                                       const GURL& url) {
  return GetEffectiveURL(browser_context, url) != url;
}

// static
bool SiteInstanceImpl::DoesSiteRequireDedicatedProcess(
    const IsolationContext& isolation_context,
    const GURL& url) {
  DCHECK_CURRENTLY_ON(BrowserThread::UI);
  return SiteIsolationPolicy::UseDedicatedProcessesForAllSites() ||
         DoesSiteURLRequireDedicatedProcess(
             isolation_context,
             SiteInstanceImpl::ComputeSiteInfo(isolation_context, url)
                 .site_url());
}

// static
bool SiteInstanceImpl::DoesSiteURLRequireDedicatedProcess(
    const IsolationContext& isolation_context,
    const GURL& site_url) {
  DCHECK_CURRENTLY_ON(BrowserThread::UI);
  DCHECK(isolation_context.browser_or_resource_context());

  // If --site-per-process is enabled, site isolation is enabled everywhere.
  if (SiteIsolationPolicy::UseDedicatedProcessesForAllSites())
    return true;

  // Always require a dedicated process for isolated origins.
  auto* policy = ChildProcessSecurityPolicyImpl::GetInstance();
  if (policy->IsIsolatedOrigin(isolation_context,
                               url::Origin::Create(site_url)))
    return true;

  // Error pages in main frames do require isolation, however since this is
  // missing the context whether this is for a main frame or not, that part
  // is enforced in RenderFrameHostManager.
  if (site_url.SchemeIs(kChromeErrorScheme))
    return true;

  // Isolate WebUI pages from one another and from other kinds of schemes.
  for (const auto& webui_scheme : URLDataManagerBackend::GetWebUISchemes()) {
    if (site_url.SchemeIs(webui_scheme))
      return true;
  }

  // Let the content embedder enable site isolation for specific URLs. Use the
  // canonical site url for this check, so that schemes with nested origins
  // (blob and filesystem) work properly.
  if (GetContentClient()->browser()->DoesSiteRequireDedicatedProcess(
          isolation_context.browser_or_resource_context().ToBrowserContext(),
          site_url)) {
    return true;
  }

  return false;
}

// static
bool SiteInstanceImpl::ShouldLockProcess(
    const IsolationContext& isolation_context,
    const GURL& site_url,
    const bool is_guest) {
  DCHECK_CURRENTLY_ON(BrowserThread::UI);
  BrowserContext* browser_context =
      isolation_context.browser_or_resource_context().ToBrowserContext();
  DCHECK(browser_context);

  // Don't lock to origin in --single-process mode, since this mode puts
  // cross-site pages into the same process.  Note that this also covers the
  // single-process mode in Android Webview.
  if (RenderProcessHost::run_renderer_in_process())
    return false;

  if (!DoesSiteURLRequireDedicatedProcess(isolation_context, site_url))
    return false;

  // Guest processes cannot be locked to their site because guests always have
  // a fixed SiteInstance. The site of GURLs a guest loads doesn't match that
  // SiteInstance. So we skip locking the guest process to the site.
  // TODO(ncarter): Remove this exclusion once we can make origin lock per
  // RenderFrame routing id.
  if (is_guest)
    return false;

  // Most WebUI processes should be locked on all platforms.  The only exception
  // is NTP, handled via the separate callout to the embedder.
  const auto& webui_schemes = URLDataManagerBackend::GetWebUISchemes();
  if (base::Contains(webui_schemes, site_url.scheme())) {
    return GetContentClient()->browser()->DoesWebUISchemeRequireProcessLock(
        site_url.scheme());
  }

  // TODO(creis, nick): Until we can handle sites with effective URLs at the
  // call sites of ChildProcessSecurityPolicy::CanAccessDataForOrigin, we
  // must give the embedder a chance to exempt some sites to avoid process
  // kills.
  if (!GetContentClient()->browser()->ShouldLockProcess(browser_context,
                                                        site_url)) {
    return false;
  }

  return true;
}

void SiteInstanceImpl::RenderProcessHostDestroyed(RenderProcessHost* host) {
  DCHECK_EQ(process_, host);
  process_->RemoveObserver(this);
  process_ = nullptr;
  agent_scheduling_group_ = nullptr;
}

void SiteInstanceImpl::RenderProcessExited(
    RenderProcessHost* host,
    const ChildProcessTerminationInfo& info) {
  for (auto& observer : observers_)
    observer.RenderProcessGone(this, info);
}

void SiteInstanceImpl::LockProcessIfNeeded() {
  DCHECK(HasSite());

  // From now on, this process should be considered "tainted" for future
  // process reuse decisions:
  // (1) If |site_info_| required a dedicated process, this SiteInstance's
  //     process can only host URLs for the same site.
  // (2) Even if |site_info_| does not require a dedicated process, this
  //     SiteInstance's process still cannot be reused to host other sites
  //     requiring dedicated sites in the future.
  // We can get here either when we commit a URL into a SiteInstance that does
  // not yet have a site, or when we create a process for a SiteInstance with a
  // preassigned site.
  process_->SetIsUsed();

  ChildProcessSecurityPolicyImpl* policy =
      ChildProcessSecurityPolicyImpl::GetInstance();
  ProcessLock process_lock = policy->GetProcessLock(process_->GetID());
  if (ShouldLockProcess(GetIsolationContext(), site_info_.site_url(),
                        IsGuest())) {
    // Sanity check that this won't try to assign an origin lock to a <webview>
    // process, which can't be locked.
    CHECK(!process_->IsForGuestsOnly());

    ProcessLock lock_to_set = GetProcessLock();
    if (process_lock.is_empty()) {
      // TODO(nick): When all sites are isolated, this operation provides
      // strong protection. If only some sites are isolated, we need
      // additional logic to prevent the non-isolated sites from requesting
      // resources for isolated sites. https://crbug.com/509125
      TRACE_EVENT2("navigation", "RenderProcessHost::SetProcessLock", "site id",
                   id_, "lock", lock_to_set.ToString());
      process_->SetProcessLock(GetIsolationContext(), lock_to_set);
    } else if (process_lock != lock_to_set) {
      // We should never attempt to reassign a different origin lock to a
      // process.
      base::debug::SetCrashKeyString(bad_message::GetRequestedSiteURLKey(),
                                     site_info_.GetDebugString());
      policy->LogKilledProcessOriginLock(process_->GetID());
      CHECK(false) << "Trying to lock a process to " << lock_to_set.ToString()
                   << " but the process is already locked to "
                   << process_lock.ToString();
    } else {
      // Process already has the right origin lock assigned.  This case will
      // happen for commits to |site_info_| after the first one.
    }
  } else {
    // If the site that we've just committed doesn't require a dedicated
    // process, make sure we aren't putting it in a process for a site that
    // does.
    if (!process_lock.is_empty()) {
      base::debug::SetCrashKeyString(bad_message::GetRequestedSiteURLKey(),
                                     site_info_.GetDebugString());
      policy->LogKilledProcessOriginLock(process_->GetID());
      CHECK(false) << "Trying to commit non-isolated site " << site_info_
                   << " in process locked to " << process_lock.lock_url();
    }
  }

  // Track which isolation contexts use the given process.  This lets
  // ChildProcessSecurityPolicyImpl (e.g. CanAccessDataForOrigin) determine
  // whether a given URL should require a lock or not (a dynamically isolated
  // origin may require a lock in some isolation contexts but not in others).
  policy->IncludeIsolationContext(process_->GetID(), GetIsolationContext());
}

// static
void SiteInstance::StartIsolatingSite(BrowserContext* context,
                                      const GURL& url) {
  if (!SiteIsolationPolicy::AreDynamicIsolatedOriginsEnabled())
    return;

  // Ignore attempts to isolate origins that are not supported.  Do this here
  // instead of relying on AddIsolatedOrigins()'s internal validation, to avoid
  // the runtime warning generated by the latter.
  url::Origin origin(url::Origin::Create(url));
  if (!IsolatedOriginUtil::IsValidIsolatedOrigin(origin))
    return;

  // Convert |url| to a site, to avoid breaking document.domain.  Note that
  // this doesn't use effective URL resolution or other special cases from
  // GetSiteForURL() and simply converts |origin| to a scheme and eTLD+1.
  GURL site(SiteInstanceImpl::GetSiteForOrigin(origin));

  ChildProcessSecurityPolicyImpl* policy =
      ChildProcessSecurityPolicyImpl::GetInstance();
  url::Origin site_origin(url::Origin::Create(site));
  policy->AddIsolatedOrigins(
      {site_origin},
      ChildProcessSecurityPolicy::IsolatedOriginSource::USER_TRIGGERED,
      context);

  // This function currently assumes the new isolated site should persist
  // across restarts, so ask the embedder to save it, excluding off-the-record
  // profiles.
  if (!context->IsOffTheRecord())
    GetContentClient()->browser()->PersistIsolatedOrigin(context, site_origin);
}

}  // namespace content
