// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "components/printing/browser/print_composite_client.h"

#include <utility>

#include "base/bind.h"
#include "base/memory/read_only_shared_memory_region.h"
#include "base/stl_util.h"
#include "build/build_config.h"
#include "components/discardable_memory/service/discardable_shared_memory_manager.h"
#include "components/printing/browser/service_sandbox_type.h"
#include "components/printing/common/print_messages.h"
#include "components/services/print_compositor/public/cpp/print_service_mojo_types.h"
#include "components/strings/grit/components_strings.h"
#include "content/public/browser/browser_task_traits.h"
#include "content/public/browser/browser_thread.h"
#include "content/public/browser/render_frame_host.h"
#include "content/public/browser/render_process_host.h"
#include "content/public/browser/service_process_host.h"
#include "printing/common/metafile_utils.h"
#include "printing/printing_utils.h"
#include "third_party/blink/public/common/associated_interfaces/associated_interface_provider.h"

namespace printing {

namespace {

uint64_t GenerateFrameGuid(content::RenderFrameHost* render_frame_host) {
  int process_id = render_frame_host->GetProcess()->GetID();
  int frame_id = render_frame_host->GetRoutingID();
  return static_cast<uint64_t>(process_id) << 32 | frame_id;
}

// Converts a ContentToProxyIdMap to ContentToFrameMap.
// ContentToProxyIdMap maps content id to the routing id of its corresponding
// render frame proxy. This is generated when the content holder was created;
// ContentToFrameMap maps content id to its render frame's global unique id.
// The global unique id has the render process id concatenated with render
// frame routing id, which can uniquely identify a render frame.
ContentToFrameMap ConvertContentInfoMap(
    content::RenderFrameHost* render_frame_host,
    const ContentToProxyIdMap& content_proxy_map) {
  ContentToFrameMap content_frame_map;
  int process_id = render_frame_host->GetProcess()->GetID();
  for (const auto& entry : content_proxy_map) {
    auto content_id = entry.first;
    auto proxy_id = entry.second;
    // Find the RenderFrameHost that the proxy id corresponds to.
    content::RenderFrameHost* rfh =
        content::RenderFrameHost::FromPlaceholderId(process_id, proxy_id);
    if (!rfh) {
      // If the corresponding RenderFrameHost cannot be found, just skip it.
      continue;
    }

    // Store this frame's global unique id into the map.
    content_frame_map[content_id] = GenerateFrameGuid(rfh);
  }
  return content_frame_map;
}

void BindDiscardableSharedMemoryManagerOnIOThread(
    mojo::PendingReceiver<
        discardable_memory::mojom::DiscardableSharedMemoryManager> receiver) {
  DCHECK_CURRENTLY_ON(content::BrowserThread::IO);
  discardable_memory::DiscardableSharedMemoryManager::Get()->Bind(
      std::move(receiver));
}

}  // namespace

PrintCompositeClient::PrintCompositeClient(content::WebContents* web_contents)
    : content::WebContentsObserver(web_contents) {}

PrintCompositeClient::~PrintCompositeClient() {}

bool PrintCompositeClient::OnMessageReceived(
    const IPC::Message& message,
    content::RenderFrameHost* render_frame_host) {
#if BUILDFLAG(ENABLE_TAGGED_PDF)
  bool handled = true;
  IPC_BEGIN_MESSAGE_MAP_WITH_PARAM(PrintCompositeClient, message,
                                   render_frame_host)
    IPC_MESSAGE_HANDLER(PrintHostMsg_AccessibilityTree, OnAccessibilityTree)
    IPC_MESSAGE_UNHANDLED(handled = false)
  IPC_END_MESSAGE_MAP()
  return handled;
#else
  return false;
#endif
}

void PrintCompositeClient::RenderFrameDeleted(
    content::RenderFrameHost* render_frame_host) {
  if (document_cookie_ == 0)
    return;

  auto iter = pending_subframes_.find(render_frame_host);
  if (iter != pending_subframes_.end()) {
    // When a subframe we are expecting is deleted, we should notify the print
    // compositor service.
    auto* compositor = GetCompositeRequest(document_cookie_);
    compositor->NotifyUnavailableSubframe(GenerateFrameGuid(render_frame_host));
    pending_subframes_.erase(iter);
  }

  print_render_frames_.erase(render_frame_host);
}

void PrintCompositeClient::OnDidPrintFrameContent(
    int render_process_id,
    int render_frame_id,
    int document_cookie,
    mojom::DidPrintContentParamsPtr params) {
  auto* outer_contents = web_contents()->GetOuterWebContents();
  if (outer_contents) {
    // When the printed content belongs to an extension or app page, the print
    // composition needs to be handled by its outer content.
    // TODO(weili): so far, we don't have printable web contents nested in more
    // than one level. In the future, especially after PDF plugin is moved to
    // OOPIF-based webview, we should check whether we need to handle web
    // contents nested in multiple layers.
    auto* outer_client = PrintCompositeClient::FromWebContents(outer_contents);
    DCHECK(outer_client);
    outer_client->OnDidPrintFrameContent(render_process_id, render_frame_id,
                                         document_cookie, std::move(params));
    return;
  }

  if (document_cookie_ != document_cookie)
    return;

  auto* render_frame_host =
      content::RenderFrameHost::FromID(render_process_id, render_frame_id);
  if (!render_frame_host)
    return;

  // Content in |params| is sent from untrusted source; only minimal processing
  // is done here. Most of it will be directly forwarded to print compositor
  // service.
  auto* compositor = GetCompositeRequest(document_cookie);
  auto region = params->metafile_data_region.Duplicate();
  compositor->AddSubframeContent(
      GenerateFrameGuid(render_frame_host), std::move(region),
      ConvertContentInfoMap(render_frame_host, params->subframe_content_info));

  // Update our internal states about this frame.
  pending_subframes_.erase(render_frame_host);
  printed_subframes_.insert(render_frame_host);
}

#if BUILDFLAG(ENABLE_TAGGED_PDF)
void PrintCompositeClient::OnAccessibilityTree(
    int document_cookie,
    const ui::AXTreeUpdate& accessibility_tree) {
  auto* compositor = GetCompositeRequest(document_cookie);
  compositor->SetAccessibilityTree(accessibility_tree);
}
#endif

void PrintCompositeClient::PrintCrossProcessSubframe(
    const gfx::Rect& rect,
    int document_cookie,
    content::RenderFrameHost* subframe_host) {
  auto params = mojom::PrintFrameContentParams::New(rect, document_cookie);
  if (!subframe_host->IsRenderFrameLive()) {
    // When the subframe is dead, no need to send message,
    // just notify the service.
    auto* compositor = GetCompositeRequest(document_cookie);
    compositor->NotifyUnavailableSubframe(GenerateFrameGuid(subframe_host));
    return;
  }

  // If this frame is already printed, no need to print again.
  if (base::Contains(pending_subframes_, subframe_host) ||
      base::Contains(printed_subframes_, subframe_host)) {
    return;
  }

  // Send the request to the destination frame.
  int render_process_id = subframe_host->GetProcess()->GetID();
  int render_frame_id = subframe_host->GetRoutingID();
  GetPrintRenderFrame(subframe_host)
      ->PrintFrameContent(
          std::move(params),
          base::BindOnce(&PrintCompositeClient::OnDidPrintFrameContent,
                         weak_ptr_factory_.GetWeakPtr(), render_process_id,
                         render_frame_id));
  pending_subframes_.insert(subframe_host);
}

void PrintCompositeClient::DoCompositePageToPdf(
    int document_cookie,
    content::RenderFrameHost* render_frame_host,
    const mojom::DidPrintContentParams& content,
    mojom::PrintCompositor::CompositePageToPdfCallback callback) {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);

  auto* compositor = GetCompositeRequest(document_cookie);
  auto region = content.metafile_data_region.Duplicate();
  compositor->CompositePageToPdf(
      GenerateFrameGuid(render_frame_host), std::move(region),
      ConvertContentInfoMap(render_frame_host, content.subframe_content_info),
      base::BindOnce(&PrintCompositeClient::OnDidCompositePageToPdf,
                     std::move(callback)));
}

void PrintCompositeClient::DoPrepareForDocumentToPdf(
    int document_cookie,
    mojom::PrintCompositor::PrepareForDocumentToPdfCallback callback) {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
  DCHECK(!GetIsDocumentConcurrentlyComposited(document_cookie));

  auto* compositor = CreateCompositeRequest(document_cookie);
  is_doc_concurrently_composited_ = true;
  compositor->PrepareForDocumentToPdf(
      base::BindOnce(&PrintCompositeClient::OnDidPrepareForDocumentToPdf,
                     std::move(callback)));
}

void PrintCompositeClient::DoCompleteDocumentToPdf(
    int document_cookie,
    uint32_t pages_count,
    mojom::PrintCompositor::CompleteDocumentToPdfCallback callback) {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
  DCHECK(GetIsDocumentConcurrentlyComposited(document_cookie));

  auto* compositor = GetCompositeRequest(document_cookie);

  // Since this class owns compositor, compositor will be gone when this class
  // is destructed. Mojo won't call its callback in that case so it is safe to
  // use unretained |this| pointer here.
  compositor->CompleteDocumentToPdf(
      pages_count,
      base::BindOnce(&PrintCompositeClient::OnDidCompleteDocumentToPdf,
                     base::Unretained(this), document_cookie,
                     std::move(callback)));
}

void PrintCompositeClient::DoCompositeDocumentToPdf(
    int document_cookie,
    content::RenderFrameHost* render_frame_host,
    const mojom::DidPrintContentParams& content,
    mojom::PrintCompositor::CompositeDocumentToPdfCallback callback) {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
  DCHECK(!GetIsDocumentConcurrentlyComposited(document_cookie));

  auto* compositor = CreateCompositeRequest(document_cookie);
  auto region = content.metafile_data_region.Duplicate();

  // Since this class owns compositor, compositor will be gone when this class
  // is destructed. Mojo won't call its callback in that case so it is safe to
  // use unretained |this| pointer here.
  compositor->CompositeDocumentToPdf(
      GenerateFrameGuid(render_frame_host), std::move(region),
      ConvertContentInfoMap(render_frame_host, content.subframe_content_info),
      base::BindOnce(&PrintCompositeClient::OnDidCompositeDocumentToPdf,
                     base::Unretained(this), document_cookie,
                     std::move(callback)));
}

// static
void PrintCompositeClient::OnDidCompositePageToPdf(
    mojom::PrintCompositor::CompositePageToPdfCallback callback,
    mojom::PrintCompositor::Status status,
    base::ReadOnlySharedMemoryRegion region) {
  std::move(callback).Run(status, std::move(region));
}

void PrintCompositeClient::OnDidCompositeDocumentToPdf(
    int document_cookie,
    mojom::PrintCompositor::CompositeDocumentToPdfCallback callback,
    mojom::PrintCompositor::Status status,
    base::ReadOnlySharedMemoryRegion region) {
  RemoveCompositeRequest(document_cookie);
  std::move(callback).Run(status, std::move(region));
}

// static
void PrintCompositeClient::OnDidPrepareForDocumentToPdf(
    mojom::PrintCompositor::PrepareForDocumentToPdfCallback callback,
    mojom::PrintCompositor::Status status) {
  std::move(callback).Run(status);
}

void PrintCompositeClient::OnDidCompleteDocumentToPdf(
    int document_cookie,
    mojom::PrintCompositor::CompleteDocumentToPdfCallback callback,
    mojom::PrintCompositor::Status status,
    base::ReadOnlySharedMemoryRegion region) {
  RemoveCompositeRequest(document_cookie);
  std::move(callback).Run(status, std::move(region));
}

bool PrintCompositeClient::GetIsDocumentConcurrentlyComposited(
    int cookie) const {
  return is_doc_concurrently_composited_ && document_cookie_ == cookie;
}

mojom::PrintCompositor* PrintCompositeClient::CreateCompositeRequest(
    int cookie) {
  if (document_cookie_ != 0) {
    DCHECK_NE(document_cookie_, cookie);
    RemoveCompositeRequest(document_cookie_);
  }
  document_cookie_ = cookie;

  compositor_ = content::ServiceProcessHost::Launch<mojom::PrintCompositor>(
      content::ServiceProcessHost::Options()
          .WithDisplayName(IDS_PRINT_COMPOSITOR_SERVICE_DISPLAY_NAME)
          .Pass());

  mojo::PendingRemote<discardable_memory::mojom::DiscardableSharedMemoryManager>
      discardable_memory_manager;
  content::GetIOThreadTaskRunner({})->PostTask(
      FROM_HERE,
      base::BindOnce(
          &BindDiscardableSharedMemoryManagerOnIOThread,
          discardable_memory_manager.InitWithNewPipeAndPassReceiver()));
  compositor_->SetDiscardableSharedMemoryManager(
      std::move(discardable_memory_manager));
  compositor_->SetWebContentsURL(web_contents()->GetLastCommittedURL());
  compositor_->SetUserAgent(user_agent_);

  return compositor_.get();
}

void PrintCompositeClient::RemoveCompositeRequest(int cookie) {
  DCHECK_EQ(document_cookie_, cookie);
  compositor_.reset();
  document_cookie_ = 0;

  // Clear all stored printed and pending subframes.
  pending_subframes_.clear();
  printed_subframes_.clear();

  // No longer concurrently compositing this document.
  is_doc_concurrently_composited_ = false;
}

mojom::PrintCompositor* PrintCompositeClient::GetCompositeRequest(
    int cookie) const {
  DCHECK_NE(0, document_cookie_);
  DCHECK_EQ(document_cookie_, cookie);
  DCHECK(compositor_.is_bound());
  return compositor_.get();
}

const mojo::AssociatedRemote<mojom::PrintRenderFrame>&
PrintCompositeClient::GetPrintRenderFrame(content::RenderFrameHost* rfh) {
  auto it = print_render_frames_.find(rfh);
  if (it == print_render_frames_.end()) {
    mojo::AssociatedRemote<mojom::PrintRenderFrame> remote;
    rfh->GetRemoteAssociatedInterfaces()->GetInterface(&remote);
    it = print_render_frames_.emplace(rfh, std::move(remote)).first;
  }

  return it->second;
}

WEB_CONTENTS_USER_DATA_KEY_IMPL(PrintCompositeClient)

}  // namespace printing