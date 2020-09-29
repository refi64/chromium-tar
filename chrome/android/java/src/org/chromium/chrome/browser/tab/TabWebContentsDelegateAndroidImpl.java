// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.chrome.browser.tab;

import android.graphics.Rect;
import android.graphics.RectF;
import android.os.Handler;
import android.view.KeyEvent;

import androidx.annotation.VisibleForTesting;

import org.chromium.base.BuildInfo;
import org.chromium.base.ContextUtils;
import org.chromium.base.ObserverList.RewindableIterator;
import org.chromium.base.annotations.CalledByNative;
import org.chromium.base.annotations.NativeMethods;
import org.chromium.chrome.browser.AppHooks;
import org.chromium.chrome.browser.media.MediaCaptureNotificationService;
import org.chromium.chrome.browser.policy.PolicyAuditor;
import org.chromium.chrome.browser.policy.PolicyAuditorJni;
import org.chromium.components.find_in_page.FindMatchRectsDetails;
import org.chromium.components.find_in_page.FindNotificationDetails;
import org.chromium.content_public.browser.InvalidateTypes;
import org.chromium.content_public.browser.WebContents;
import org.chromium.content_public.common.ResourceRequestBody;

/**
 * Implementation class of {@link TabWebContentsDelegateAndroid}.
 */
final class TabWebContentsDelegateAndroidImpl extends TabWebContentsDelegateAndroid {
    private final TabImpl mTab;
    private final TabWebContentsDelegateAndroid mDelegate;
    private final Handler mHandler;
    private final Runnable mCloseContentsRunnable;

    public TabWebContentsDelegateAndroidImpl(TabImpl tab, TabWebContentsDelegateAndroid delegate) {
        mTab = tab;
        mDelegate = delegate;
        mHandler = new Handler();
        mCloseContentsRunnable = () -> {
            RewindableIterator<TabObserver> observers = mTab.getTabObservers();
            while (observers.hasNext()) observers.next().onCloseContents(mTab);
        };
    }

    @CalledByNative
    private void onFindResultAvailable(FindNotificationDetails result) {
        RewindableIterator<TabObserver> observers = mTab.getTabObservers();
        while (observers.hasNext()) observers.next().onFindResultAvailable(result);
    }

    @CalledByNative
    private void onFindMatchRectsAvailable(FindMatchRectsDetails result) {
        RewindableIterator<TabObserver> observers = mTab.getTabObservers();
        while (observers.hasNext()) observers.next().onFindMatchRectsAvailable(result);
    }

    // Helper functions used to create types that are part of the public interface
    @CalledByNative
    private static Rect createRect(int x, int y, int right, int bottom) {
        return new Rect(x, y, right, bottom);
    }

    @CalledByNative
    private static RectF createRectF(float x, float y, float right, float bottom) {
        return new RectF(x, y, right, bottom);
    }

    @CalledByNative
    private static FindNotificationDetails createFindNotificationDetails(int numberOfMatches,
            Rect rendererSelectionRect, int activeMatchOrdinal, boolean finalUpdate) {
        return new FindNotificationDetails(
                numberOfMatches, rendererSelectionRect, activeMatchOrdinal, finalUpdate);
    }

    @CalledByNative
    private static FindMatchRectsDetails createFindMatchRectsDetails(
            int version, int numRects, RectF activeRect) {
        return new FindMatchRectsDetails(version, numRects, activeRect);
    }

    @CalledByNative
    private static void setMatchRectByIndex(
            FindMatchRectsDetails findMatchRectsDetails, int index, RectF rect) {
        findMatchRectsDetails.rects[index] = rect;
    }

    @CalledByNative
    @Override
    protected int getDisplayMode() {
        return mDelegate.getDisplayMode();
    }

    @CalledByNative
    @Override
    protected boolean shouldResumeRequestsForCreatedWindow() {
        return mDelegate.shouldResumeRequestsForCreatedWindow();
    }

    @CalledByNative
    @Override
    protected boolean addNewContents(WebContents sourceWebContents, WebContents webContents,
            int disposition, Rect initialPosition, boolean userGesture) {
        return mDelegate.addNewContents(
                sourceWebContents, webContents, disposition, initialPosition, userGesture);
    }

    // WebContentsDelegateAndroid

    @Override
    public void openNewTab(String url, String extraHeaders, ResourceRequestBody postData,
            int disposition, boolean isRendererInitiated) {
        mDelegate.openNewTab(url, extraHeaders, postData, disposition, isRendererInitiated);
    }

    @Override
    public void activateContents() {
        mDelegate.activateContents();
    }

    @Override
    public boolean addMessageToConsole(int level, String message, int lineNumber, String sourceId) {
        // Only output console.log messages on debug variants of Android OS. crbug/869804
        return !BuildInfo.isDebugAndroid();
    }

    @Override
    public void loadingStateChanged(boolean toDifferentDocument) {
        boolean isLoading = mTab.getWebContents() != null && mTab.getWebContents().isLoading();
        if (isLoading) {
            mTab.onLoadStarted(toDifferentDocument);
        } else {
            mTab.onLoadStopped();
        }
        mDelegate.loadingStateChanged(toDifferentDocument);
    }

    @Override
    public void onUpdateUrl(String url) {
        RewindableIterator<TabObserver> observers = mTab.getTabObservers();
        while (observers.hasNext()) observers.next().onUpdateUrl(mTab, url);
        mDelegate.onUpdateUrl(url);
    }

    @Override
    public boolean takeFocus(boolean reverse) {
        return mDelegate.takeFocus(reverse);
    }

    @Override
    public void handleKeyboardEvent(KeyEvent event) {
        mDelegate.handleKeyboardEvent(event);
    }

    @Override
    public void enterFullscreenModeForTab(boolean prefersNavigationBar) {
        mDelegate.enterFullscreenModeForTab(prefersNavigationBar);
    }

    @Override
    public void exitFullscreenModeForTab() {
        mDelegate.exitFullscreenModeForTab();
    }

    @Override
    public boolean isFullscreenForTabOrPending() {
        return mDelegate.isFullscreenForTabOrPending();
    }

    @Override
    public void navigationStateChanged(int flags) {
        if ((flags & InvalidateTypes.TAB) != 0) {
            MediaCaptureNotificationService.updateMediaNotificationForTab(
                    ContextUtils.getApplicationContext(), mTab.getId(), mTab.getWebContents(),
                    mTab.getUrlString());
        }
        if ((flags & InvalidateTypes.TITLE) != 0) {
            // Update cached title then notify observers.
            mTab.updateTitle();
        }
        if ((flags & InvalidateTypes.URL) != 0) {
            RewindableIterator<TabObserver> observers = mTab.getTabObservers();
            while (observers.hasNext()) observers.next().onUrlUpdated(mTab);
        }
        mDelegate.navigationStateChanged(flags);
    }

    @Override
    public void visibleSSLStateChanged() {
        PolicyAuditor auditor = AppHooks.get().getPolicyAuditor();
        auditor.notifyCertificateFailure(
                PolicyAuditorJni.get().getCertificateFailure(mTab.getWebContents()),
                ContextUtils.getApplicationContext());
        RewindableIterator<TabObserver> observers = mTab.getTabObservers();
        while (observers.hasNext()) observers.next().onSSLStateUpdated(mTab);
        mDelegate.visibleSSLStateChanged();
    }

    @Override
    public boolean shouldCreateWebContents(String targetUrl) {
        return mDelegate.shouldCreateWebContents(targetUrl);
    }

    @Override
    public void webContentsCreated(WebContents sourceWebContents, long openerRenderProcessId,
            long openerRenderFrameId, String frameName, String targetUrl,
            WebContents newWebContents) {
        RewindableIterator<TabObserver> observers = mTab.getTabObservers();
        while (observers.hasNext()) {
            observers.next().webContentsCreated(mTab, sourceWebContents, openerRenderProcessId,
                    openerRenderFrameId, frameName, targetUrl, newWebContents);
        }
        mDelegate.webContentsCreated(sourceWebContents, openerRenderProcessId, openerRenderFrameId,
                frameName, targetUrl, newWebContents);
    }

    @Override
    public void showRepostFormWarningDialog() {
        mDelegate.showRepostFormWarningDialog();
    }

    @Override
    public boolean shouldBlockMediaRequest(String url) {
        return mDelegate.shouldBlockMediaRequest(url);
    }

    @Override
    public void rendererUnresponsive() {
        if (mTab.getWebContents() != null) {
            TabWebContentsDelegateAndroidImplJni.get().onRendererUnresponsive(
                    mTab.getWebContents());
        }
        mTab.handleRendererResponsiveStateChanged(false);
        mDelegate.rendererUnresponsive();
    }

    @Override
    public void rendererResponsive() {
        if (mTab.getWebContents() != null) {
            TabWebContentsDelegateAndroidImplJni.get().onRendererResponsive(mTab.getWebContents());
        }
        mTab.handleRendererResponsiveStateChanged(true);
        mDelegate.rendererResponsive();
    }

    @Override
    public void closeContents() {
        // Execute outside of callback, otherwise we end up deleting the native
        // objects in the middle of executing methods on them.
        mHandler.removeCallbacks(mCloseContentsRunnable);
        mHandler.post(mCloseContentsRunnable);
        mDelegate.closeContents();
    }

    @CalledByNative
    @Override
    protected void setOverlayMode(boolean useOverlayMode) {
        mDelegate.setOverlayMode(useOverlayMode);
    }

    @CalledByNative
    @Override
    protected boolean shouldEnableEmbeddedMediaExperience() {
        return mDelegate.shouldEnableEmbeddedMediaExperience();
    }

    /**
     * @return web preferences for enabling Picture-in-Picture.
     */
    @CalledByNative
    @Override
    protected boolean isPictureInPictureEnabled() {
        return mDelegate.isPictureInPictureEnabled();
    }

    /**
     * @return Night mode enabled/disabled for this Tab. To be used to propagate
     *         the preferred color scheme to the renderer.
     */
    @CalledByNative
    @Override
    protected boolean isNightModeEnabled() {
        return mDelegate.isNightModeEnabled();
    }

    /**
     * Return true if app banners are to be permitted in this tab. May need to be overridden.
     * @return true if app banners are permitted, and false otherwise.
     */
    @CalledByNative
    @Override
    protected boolean canShowAppBanners() {
        return mDelegate.canShowAppBanners();
    }

    /**
     * @return the WebAPK manifest scope. This gives frames within the scope increased privileges
     * such as autoplaying media unmuted.
     */
    @CalledByNative
    @Override
    protected String getManifestScope() {
        return mDelegate.getManifestScope();
    }

    /**
     * Checks if the associated tab is currently presented in the context of custom tabs.
     * @return true if this is currently a custom tab.
     */
    @CalledByNative
    @Override
    protected boolean isCustomTab() {
        return mDelegate.isCustomTab();
    }

    /**
     * Checks if the associated tab is running an activity for installed webapp (TWA only for now),
     * and whether the geolocation request should be delegated to the client app.
     * @return true if this is TWA and should delegate geolocation request.
     */
    @CalledByNative
    @Override
    protected boolean isInstalledWebappDelegateGeolocation() {
        return mDelegate.isInstalledWebappDelegateGeolocation();
    }

    @Override
    public int getTopControlsHeight() {
        return mDelegate.getTopControlsHeight();
    }

    @Override
    public int getTopControlsMinHeight() {
        return mDelegate.getTopControlsMinHeight();
    }

    @Override
    public int getBottomControlsHeight() {
        return mDelegate.getBottomControlsHeight();
    }

    @Override
    public int getBottomControlsMinHeight() {
        return mDelegate.getBottomControlsMinHeight();
    }

    @Override
    public boolean shouldAnimateBrowserControlsHeightChanges() {
        return mDelegate.shouldAnimateBrowserControlsHeightChanges();
    }

    @Override
    public boolean controlsResizeView() {
        return mDelegate.controlsResizeView();
    }

    @VisibleForTesting
    void showFramebustBlockInfobarForTesting(String url) {
        TabWebContentsDelegateAndroidImplJni.get().showFramebustBlockInfoBar(
                mTab.getWebContents(), url);
    }

    @NativeMethods
    interface Natives {
        void onRendererUnresponsive(WebContents webContents);
        void onRendererResponsive(WebContents webContents);
        void showFramebustBlockInfoBar(WebContents webContents, String url);
    }
}