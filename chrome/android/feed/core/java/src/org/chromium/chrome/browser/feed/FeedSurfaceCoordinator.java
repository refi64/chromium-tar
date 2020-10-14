// Copyright 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.chrome.browser.feed;

import android.app.Activity;
import android.content.Context;
import android.content.res.Configuration;
import android.content.res.Resources;
import android.graphics.Canvas;
import android.os.Build;
import android.os.Handler;
import android.os.SystemClock;
import android.view.LayoutInflater;
import android.view.MotionEvent;
import android.view.View;
import android.widget.FrameLayout;
import android.widget.ScrollView;

import androidx.annotation.Nullable;
import androidx.annotation.VisibleForTesting;

import org.chromium.base.ApiCompatibilityUtils;
import org.chromium.base.FeatureList;
import org.chromium.base.supplier.Supplier;
import org.chromium.chrome.R;
import org.chromium.chrome.browser.app.ChromeActivity;
import org.chromium.chrome.browser.feature_engagement.TrackerFactory;
import org.chromium.chrome.browser.feed.action.FeedActionHandler;
import org.chromium.chrome.browser.feed.library.api.host.action.ActionApi;
import org.chromium.chrome.browser.feed.shared.FeedSurfaceDelegate;
import org.chromium.chrome.browser.feed.shared.FeedSurfaceProvider;
import org.chromium.chrome.browser.feed.shared.stream.Header;
import org.chromium.chrome.browser.feed.shared.stream.NonDismissibleHeader;
import org.chromium.chrome.browser.feed.shared.stream.Stream;
import org.chromium.chrome.browser.feed.v2.FeedStream;
import org.chromium.chrome.browser.flags.ChromeFeatureList;
import org.chromium.chrome.browser.native_page.ContextMenuManager;
import org.chromium.chrome.browser.native_page.NativePageNavigationDelegate;
import org.chromium.chrome.browser.ntp.NewTabPageLayout;
import org.chromium.chrome.browser.ntp.SnapScrollHelper;
import org.chromium.chrome.browser.ntp.cards.promo.HomepagePromoController;
import org.chromium.chrome.browser.ntp.snippets.SectionHeaderView;
import org.chromium.chrome.browser.profiles.Profile;
import org.chromium.chrome.browser.signin.PersonalizedSigninPromoView;
import org.chromium.chrome.browser.tab.Tab;
import org.chromium.chrome.browser.tabmodel.TabModelSelector;
import org.chromium.chrome.browser.ui.messages.snackbar.SnackbarManager;
import org.chromium.chrome.browser.user_education.UserEducationHelper;
import org.chromium.chrome.features.start_surface.StartSurfaceConfiguration;
import org.chromium.components.browser_ui.bottomsheet.BottomSheetController;
import org.chromium.components.browser_ui.util.GlobalDiscardableReferencePool;
import org.chromium.components.browser_ui.widget.displaystyle.UiConfig;
import org.chromium.components.browser_ui.widget.displaystyle.ViewResizer;
import org.chromium.components.feature_engagement.Tracker;
import org.chromium.ui.UiUtils;
import org.chromium.ui.base.ViewUtils;

import java.util.ArrayList;
import java.util.Arrays;
import java.util.List;

/**
 * Provides a surface that displays an interest feed rendered list of content suggestions.
 */
public class FeedSurfaceCoordinator implements FeedSurfaceProvider {
    @VisibleForTesting
    public static final String FEED_STREAM_CREATED_TIME_MS_UMA = "FeedStreamCreatedTime";

    private final Activity mActivity;
    private final SnackbarManager mSnackbarManager;
    @Nullable
    private final View mNtpHeader;
    private final boolean mShowDarkBackground;
    private final boolean mIsPlaceholderShown;
    private final boolean mIsPlaceholderShownInV1;
    private final boolean mV2Enabled;
    private final FeedSurfaceDelegate mDelegate;
    private final int mDefaultMargin;
    private final int mWideMargin;
    private final FeedSurfaceMediator mMediator;
    private final BottomSheetController mBottomSheetController;
    private final FeedActionHandler.Options mActionOptions;

    private UiConfig mUiConfig;
    private FrameLayout mRootView;
    private ContextMenuManager mContextMenuManager;
    private Tracker mTracker;
    private long mStreamCreatedTimeMs;

    // Homepage promo view will be not-null once we have it created, until it is destroyed.
    private @Nullable View mHomepagePromoView;
    private @Nullable HomepagePromoController mHomepagePromoController;

    // Used when Feed is enabled.
    private @Nullable Stream mStream;
    private @Nullable FeedImageLoader mImageLoader;
    private @Nullable StreamLifecycleManager mStreamLifecycleManager;
    private @Nullable SectionHeaderView mSectionHeaderView;
    private @Nullable PersonalizedSigninPromoView mSigninPromoView;
    private @Nullable ViewResizer mStreamViewResizer;
    private @Nullable NativePageNavigationDelegate mPageNavigationDelegate;
    private @Nullable Profile mProfile;

    // Used when Feed is disabled by policy.
    private @Nullable ScrollView mScrollViewForPolicy;
    private @Nullable ViewResizer mScrollViewResizer;

    // Used for the feed header menu.
    private UserEducationHelper mUserEducationHelper;

    private final Handler mHandler = new Handler();

    private class SignInPromoHeader implements Header {
        @Override
        public View getView() {
            return getSigninPromoView();
        }

        @Override
        public boolean isDismissible() {
            return true;
        }

        @Override
        public void onDismissed() {
            mMediator.onSignInPromoDismissed();
        }
    }

    private class HomepagePromoHeader implements Header {
        @Override
        public View getView() {
            assert mHomepagePromoView != null;
            return mHomepagePromoView;
        }

        @Override
        public boolean isDismissible() {
            return true;
        }

        @Override
        public void onDismissed() {
            assert mHomepagePromoController != null;
            mHomepagePromoController.dismissPromo();
        }
    }

    /**
     * Provides the additional capabilities needed for the container view.
     */
    private class RootView extends FrameLayout {
        /**
         * @param context The context of the application.
         */
        public RootView(Context context) {
            super(context);
        }

        @Override
        protected void onConfigurationChanged(Configuration newConfig) {
            super.onConfigurationChanged(newConfig);
            mUiConfig.updateDisplayStyle();
        }

        @Override
        public boolean onInterceptTouchEvent(MotionEvent ev) {
            if (super.onInterceptTouchEvent(ev)) return true;
            if (mMediator != null && !mMediator.getTouchEnabled()) return true;

            return mDelegate.onInterceptTouchEvent(ev);
        }
    }

    /**
     * Provides the additional capabilities needed for the {@link ScrollView}.
     */
    private class PolicyScrollView extends ScrollView {
        public PolicyScrollView(Context context) {
            super(context);
        }

        @Override
        protected void onConfigurationChanged(Configuration newConfig) {
            super.onConfigurationChanged(newConfig);
            mUiConfig.updateDisplayStyle();
        }
    }

    /**
     * Constructs a new FeedSurfaceCoordinator.
     *
     * @param activity The containing {@link ChromeActivity}.
     * @param snackbarManager The {@link SnackbarManager} displaying Snackbar UI.
     * @param tabModelSelector {@link TabModelSelector} object.
     * @param tabProvider Provides the current active tab.
     * @param snapScrollHelper The {@link SnapScrollHelper} for the New Tab Page.
     * @param ntpHeader The extra header on top of the feeds for the New Tab Page.
     * @param sectionHeaderView The {@link SectionHeaderView} for the feed.
     * @param actionOptions Configures feed actions.
     * @param showDarkBackground Whether is shown on dark background.
     * @param delegate The constructing {@link FeedSurfaceDelegate}.
     * @param pageNavigationDelegate The {@link NativePageNavigationDelegate}
     *                               that handles page navigation.
     * @param profile The current user profile.
     * @param isPlaceholderShown Whether the placeholder should be shown.
     */
    public FeedSurfaceCoordinator(Activity activity, SnackbarManager snackbarManager,
            TabModelSelector tabModelSelector, Supplier<Tab> tabProvider,
            @Nullable SnapScrollHelper snapScrollHelper, @Nullable View ntpHeader,
            @Nullable SectionHeaderView sectionHeaderView, FeedActionHandler.Options actionOptions,
            boolean showDarkBackground, FeedSurfaceDelegate delegate,
            @Nullable NativePageNavigationDelegate pageNavigationDelegate, Profile profile,
            boolean isPlaceholderShown, BottomSheetController bottomSheetController) {
        mActivity = activity;
        mSnackbarManager = snackbarManager;
        mNtpHeader = ntpHeader;
        mSectionHeaderView = sectionHeaderView;
        mShowDarkBackground = showDarkBackground;
        mIsPlaceholderShown = isPlaceholderShown;
        mV2Enabled = FeatureList.isInitialized()
                && ChromeFeatureList.isEnabled(ChromeFeatureList.INTEREST_FEED_V2);
        mIsPlaceholderShownInV1 = mIsPlaceholderShown && !mV2Enabled;
        mDelegate = delegate;
        mPageNavigationDelegate = pageNavigationDelegate;
        mBottomSheetController = bottomSheetController;
        mProfile = profile;
        mActionOptions = actionOptions;

        Resources resources = mActivity.getResources();
        mDefaultMargin = resources.getDimensionPixelSize(mV2Enabled
                        ? R.dimen.content_suggestions_card_modern_margin_v2
                        : R.dimen.content_suggestions_card_modern_margin);
        mWideMargin = resources.getDimensionPixelSize(mV2Enabled
                        ? R.dimen.ntp_wide_card_lateral_margins_v2
                        : R.dimen.ntp_wide_card_lateral_margins);

        mRootView = new RootView(mActivity);
        mRootView.setPadding(0, resources.getDimensionPixelOffset(R.dimen.tab_strip_height), 0, 0);
        mUiConfig = new UiConfig(mRootView);

        mTracker = TrackerFactory.getTrackerForProfile(profile);

        if (ChromeFeatureList.isEnabled(ChromeFeatureList.HOMEPAGE_PROMO_CARD)) {
            mHomepagePromoController =
                    new HomepagePromoController(mActivity, mSnackbarManager, mTracker);
        }

        // Mediator should be created before any Stream changes.
        mMediator = new FeedSurfaceMediator(this, snapScrollHelper, mPageNavigationDelegate);

        mUserEducationHelper = new UserEducationHelper(mActivity, mHandler);
    }

    @Override
    public void destroy() {
        mMediator.destroy();
        if (mStreamLifecycleManager != null) mStreamLifecycleManager.destroy();
        mStreamLifecycleManager = null;
        if (mImageLoader != null) mImageLoader.destroy();
        mImageLoader = null;
        if (mHomepagePromoController != null) mHomepagePromoController.destroy();
    }

    @Override
    public ContextMenuManager.TouchEnabledDelegate getTouchEnabledDelegate() {
        return mMediator;
    }

    @Override
    public NewTabPageLayout.ScrollDelegate getScrollDelegate() {
        return mMediator;
    }

    @Override
    public UiConfig getUiConfig() {
        return mUiConfig;
    }

    @Override
    public View getView() {
        return mRootView;
    }

    @Override
    public boolean shouldCaptureThumbnail() {
        return mMediator.shouldCaptureThumbnail();
    }

    @Override
    public void captureThumbnail(Canvas canvas) {
        ViewUtils.captureBitmap(mRootView, canvas);
        mMediator.onThumbnailCaptured();
    }

    /**
     * @return The {@link StreamLifecycleManager} that manages the lifecycle of the {@link Stream}.
     */
    StreamLifecycleManager getStreamLifecycleManager() {
        return mStreamLifecycleManager;
    }

    /** @return The {@link Stream} that this class holds. */
    public Stream getStream() {
        return mStream;
    }

    /** @return Whether the placeholder shows in V1. */
    public boolean isPlaceholderShownInV1() {
        return mIsPlaceholderShownInV1;
    }

    /**
     * Create a {@link Stream} for this class.
     */
    void createStream() {
        if (mScrollViewForPolicy != null) {
            mRootView.removeView(mScrollViewForPolicy);
            mScrollViewForPolicy = null;
            mScrollViewResizer.detach();
            mScrollViewResizer = null;
        }

        mStreamCreatedTimeMs = SystemClock.elapsedRealtime();
        if (mV2Enabled) {
            mStream = new FeedStream(mActivity, mShowDarkBackground, mSnackbarManager,
                    mPageNavigationDelegate, mBottomSheetController);
        } else {
            FeedAppLifecycle appLifecycle = FeedProcessScopeFactory.getFeedAppLifecycle();
            appLifecycle.onNTPOpened();

            mImageLoader = new FeedImageLoader(
                    mActivity, GlobalDiscardableReferencePool.getReferencePool());

            ActionApi actionApi = new FeedActionHandler(mActionOptions, mPageNavigationDelegate,
                    FeedProcessScopeFactory.getFeedConsumptionObserver(),
                    FeedProcessScopeFactory.getFeedLoggingBridge(), mActivity, mProfile);
            mStream = FeedV1StreamCreator.createStream(mActivity, mImageLoader, actionApi,
                    mUiConfig, mSnackbarManager, mShowDarkBackground, mIsPlaceholderShownInV1);
        }

        mStreamLifecycleManager = mDelegate.createStreamLifecycleManager(mStream, mActivity);

        View view = mStream.getView();
        view.setBackgroundResource(R.color.default_bg_color);
        if (mIsPlaceholderShownInV1) {
            // Set recyclerView as transparent until first patch of articles are loaded. Before
            // that, the placeholder is shown.
            view.getBackground().setAlpha(0);
        }
        mRootView.addView(view);
        mStreamViewResizer =
                ViewResizer.createAndAttach(view, mUiConfig, mDefaultMargin, mWideMargin);

        if (mNtpHeader != null) UiUtils.removeViewFromParent(mNtpHeader);
        if (mSectionHeaderView != null) UiUtils.removeViewFromParent(mSectionHeaderView);
        if (mSigninPromoView != null) UiUtils.removeViewFromParent(mSigninPromoView);
        if (mHomepagePromoView != null) UiUtils.removeViewFromParent(mHomepagePromoView);

        if (mNtpHeader != null) {
            mStream.setHeaderViews(Arrays.asList(new NonDismissibleHeader(mNtpHeader),
                    new NonDismissibleHeader(mSectionHeaderView)));
        } else if (mSectionHeaderView != null) {
            mStream.setHeaderViews(Arrays.asList(new NonDismissibleHeader(mSectionHeaderView)));
        }

        if (!mV2Enabled) {
            mStream.addScrollListener(new FeedLoggingBridge.ScrollEventReporter(
                    FeedProcessScopeFactory.getFeedLoggingBridge()));
        }

        // Work around https://crbug.com/943873 where default focus highlight shows up after
        // toggling dark mode.
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) {
            view.setDefaultFocusHighlightEnabled(false);
        }

        // Explicitly request focus on the scroll container to avoid UrlBar being focused after
        // the scroll container for policy is removed.
        view.requestFocus();
    }

    /**
     * @return The {@link ScrollView} for displaying content for supervised user or enterprise
     *         policy.
     */
    ScrollView getScrollViewForPolicy() {
        return mScrollViewForPolicy;
    }

    /**
     * Create a {@link ScrollView} for displaying content for supervised user or enterprise policy.
     */
    void createScrollViewForPolicy() {
        if (mStream != null) {
            mStreamViewResizer.detach();
            mStreamViewResizer = null;
            mRootView.removeView(mStream.getView());
            assert mStreamLifecycleManager
                    != null
                : "StreamLifecycleManager should not be null when the Stream is not null.";
            mStreamLifecycleManager.destroy();
            mStreamLifecycleManager = null;
            // Do not call mStream.onDestroy(), the mStreamLifecycleManager has done that for us.
            mStream = null;
            mSectionHeaderView = null;
            mSigninPromoView = null;
            mHomepagePromoView = null;
            // TODO(wenyufu): Support HomepagePromo when policy enabled.
            if (mHomepagePromoController != null) {
                mHomepagePromoController.destroy();
                mHomepagePromoController = null;
            }
            if (mImageLoader != null) {
                mImageLoader.destroy();
                mImageLoader = null;
            }
        }

        mScrollViewForPolicy = new PolicyScrollView(mActivity);
        mScrollViewForPolicy.setBackgroundColor(
                ApiCompatibilityUtils.getColor(mActivity.getResources(), R.color.default_bg_color));
        mScrollViewForPolicy.setVerticalScrollBarEnabled(false);

        // Make scroll view focusable so that it is the next focusable view when the url bar clears
        // focus.
        mScrollViewForPolicy.setFocusable(true);
        mScrollViewForPolicy.setFocusableInTouchMode(true);
        mScrollViewForPolicy.setContentDescription(
                mScrollViewForPolicy.getResources().getString(R.string.accessibility_new_tab_page));

        if (mNtpHeader != null) {
            UiUtils.removeViewFromParent(mNtpHeader);
            mScrollViewForPolicy.addView(mNtpHeader);
        }
        mRootView.addView(mScrollViewForPolicy);
        mScrollViewResizer = ViewResizer.createAndAttach(
                mScrollViewForPolicy, mUiConfig, mDefaultMargin, mWideMargin);
        mScrollViewForPolicy.requestFocus();
    }

    /** @return The {@link SectionHeaderView} for the Feed section header. */
    SectionHeaderView getSectionHeaderView() {
        return mSectionHeaderView;
    }

    /** @return The {@link PersonalizedSigninPromoView} for this class. */
    PersonalizedSigninPromoView getSigninPromoView() {
        if (mSigninPromoView == null) {
            LayoutInflater inflater = LayoutInflater.from(mRootView.getContext());
            mSigninPromoView = (PersonalizedSigninPromoView) inflater.inflate(
                    R.layout.personalized_signin_promo_view_modern_content_suggestions, mRootView,
                    false);
            // If the placeholder is shown in V1, delay to show the sign-in view until the articles
            // are shown.
            if (mIsPlaceholderShownInV1) {
                mSigninPromoView.setVisibility(View.INVISIBLE);
            }
        }
        return mSigninPromoView;
    }

    /**
     *  Update header views in the Stream.
     *  */
    void updateHeaderViews(boolean isSignInPromoVisible, View homepagePromoView) {
        if (mStream == null) return;

        List<Header> headers = new ArrayList<>();
        if (mNtpHeader != null) {
            assert mSectionHeaderView != null;
            headers.add(new NonDismissibleHeader(mNtpHeader));
        }

        if (homepagePromoView != null) {
            mHomepagePromoView = homepagePromoView;
            headers.add(new HomepagePromoHeader());
        }

        if (mSectionHeaderView != null) {
            headers.add(new NonDismissibleHeader(mSectionHeaderView));
        }

        if (isSignInPromoVisible) {
            headers.add(new SignInPromoHeader());
        }

        mStream.setHeaderViews(headers);
    }

    /**
     * Determines whether the feed header position in the recycler view is suitable for IPH.
     *
     * @param maxPosFraction The maximal fraction of the recycler view height starting from the top
     *                       within which the top position of the feed header can be. The value has
     *                       to be within the range [0.0, 1.0], where at 0.0 the feed header is at
     *                       the very top of the recycler view and at 1.0 is at the very bottom and
     *                       hidden.
     * @return True If the feed header is at a position that is suitable to show the IPH.
     */
    boolean isFeedHeaderPositionInRecyclerViewSuitableForIPH(float maxPosFraction) {
        assert maxPosFraction >= 0.0f
                && maxPosFraction <= 1.0f
            : "Max position fraction should be ranging between 0.0 and 1.0";

        // Get the top position of the section header view in the recycler view.
        int[] headerPositions = new int[2];
        mSectionHeaderView.getLocationOnScreen(headerPositions);
        int topPosInStream = headerPositions[1] - mRootView.getTop();

        if (topPosInStream < 0) return false;
        if (topPosInStream > maxPosFraction * mRootView.getHeight()) return false;

        return true;
    }

    public void onOverviewShownAtLaunch(long activityCreationTimeMs) {
        mMediator.onOverviewShownAtLaunch(activityCreationTimeMs, mIsPlaceholderShown);
        StartSurfaceConfiguration.recordHistogram(FEED_STREAM_CREATED_TIME_MS_UMA,
                mStreamCreatedTimeMs - activityCreationTimeMs, mIsPlaceholderShown);
    }

    Tracker getFeatureEngagementTracker() {
        return mTracker;
    }

    UserEducationHelper getUserEducationHelper() {
        return mUserEducationHelper;
    }

    HomepagePromoController getHomepagePromoController() {
        return mHomepagePromoController;
    }

    @VisibleForTesting
    FeedSurfaceMediator getMediatorForTesting() {
        return mMediator;
    }

    @VisibleForTesting
    public View getSignInPromoViewForTesting() {
        return getSigninPromoView();
    }

    @VisibleForTesting
    public View getSectionHeaderViewForTesting() {
        return getSectionHeaderView();
    }

    @VisibleForTesting
    public Stream getStreamForTesting() {
        return getStream();
    }
}
