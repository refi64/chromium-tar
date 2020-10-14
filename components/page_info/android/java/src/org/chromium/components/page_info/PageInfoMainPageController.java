// Copyright 2020 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.components.page_info;

import org.chromium.components.embedder_support.browser_context.BrowserContextHandle;

/**
 * Interface for a page info main page controller.
 */
public interface PageInfoMainPageController {
    /**
     * Launches the PageInfoSubpage provided by |pageInfoCookiesController|.
     * @param controller The controller providing a PageInfoSubpage.
     */
    void launchSubpage(PageInfoSubpageController controller);

    /**
     * Switches back to the main page info view.
     */
    void exitSubpage();

    /**
     * @return A BrowserContext for this dialog.
     */
    BrowserContextHandle getBrowserContext();
}
