// Copyright 2020 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef COMPONENTS_PRERENDER_BROWSER_PRERENDER_PROCESSOR_IMPL_H_
#define COMPONENTS_PRERENDER_BROWSER_PRERENDER_PROCESSOR_IMPL_H_

#include "components/prerender/browser/prerender_processor_impl_delegate.h"
#include "third_party/blink/public/mojom/prerender/prerender.mojom.h"

namespace content {
class RenderFrameHost;
}  // namespace content

namespace prerender {

class PrerenderProcessorImpl : public blink::mojom::PrerenderProcessor {
 public:
  PrerenderProcessorImpl(
      int render_process_id,
      int render_frame_id,
      std::unique_ptr<PrerenderProcessorImplDelegate> delegate);
  ~PrerenderProcessorImpl() override;

  static void Create(
      content::RenderFrameHost* frame_host,
      mojo::PendingReceiver<blink::mojom::PrerenderProcessor> receiver,
      std::unique_ptr<PrerenderProcessorImplDelegate> delegate);

  // blink::mojom::PrerenderProcessor implementation
  void AddPrerender(
      blink::mojom::PrerenderAttributesPtr attributes,
      mojo::PendingRemote<blink::mojom::PrerenderHandleClient> client,
      mojo::PendingReceiver<blink::mojom::PrerenderHandle> handle) override;

 private:
  int render_process_id_;
  int render_frame_id_;
  std::unique_ptr<PrerenderProcessorImplDelegate> delegate_;
};

}  // namespace prerender

#endif  // COMPONENTS_PRERENDER_BROWSER_PRERENDER_PROCESSOR_IMPL_H_
