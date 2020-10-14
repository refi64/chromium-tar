// Copyright 2020 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.chrome.browser.signin;

import static androidx.test.espresso.Espresso.onView;
import static androidx.test.espresso.action.ViewActions.click;
import static androidx.test.espresso.matcher.ViewMatchers.withText;

import static org.mockito.MockitoAnnotations.initMocks;

import android.view.View;

import androidx.test.filters.MediumTest;

import org.junit.AfterClass;
import org.junit.Before;
import org.junit.BeforeClass;
import org.junit.Rule;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.mockito.Mock;

import org.chromium.base.test.params.ParameterAnnotations;
import org.chromium.base.test.params.ParameterizedRunner;
import org.chromium.base.test.util.CommandLineFlags;
import org.chromium.base.test.util.Feature;
import org.chromium.chrome.R;
import org.chromium.chrome.browser.app.ChromeActivity;
import org.chromium.chrome.browser.flags.ChromeFeatureList;
import org.chromium.chrome.browser.flags.ChromeSwitches;
import org.chromium.chrome.browser.night_mode.ChromeNightModeTestUtils;
import org.chromium.chrome.browser.signin.account_picker.AccountPickerBottomSheetCoordinator;
import org.chromium.chrome.browser.signin.account_picker.AccountPickerDelegate;
import org.chromium.chrome.test.ChromeActivityTestRule;
import org.chromium.chrome.test.ChromeJUnit4RunnerDelegate;
import org.chromium.chrome.test.util.browser.Features;
import org.chromium.chrome.test.util.browser.signin.AccountManagerTestRule;
import org.chromium.components.browser_ui.bottomsheet.BottomSheetController;
import org.chromium.components.signin.ProfileDataSource;
import org.chromium.components.signin.test.util.FakeProfileDataSource;
import org.chromium.content_public.browser.test.util.CriteriaHelper;
import org.chromium.content_public.browser.test.util.TestThreadUtils;
import org.chromium.ui.test.util.NightModeTestUtils;
import org.chromium.ui.test.util.RenderTestRule;

import java.io.IOException;

/**
 * Render tests of account picker bottom sheet.
 */
@RunWith(ParameterizedRunner.class)
@ParameterAnnotations.UseRunnerDelegate(ChromeJUnit4RunnerDelegate.class)
@CommandLineFlags.Add({ChromeSwitches.DISABLE_FIRST_RUN_EXPERIENCE})
@Features.EnableFeatures({ChromeFeatureList.MOBILE_IDENTITY_CONSISTENCY})
public class AccountPickerBottomSheetRenderTest {
    private static final ProfileDataSource.ProfileData PROFILE_DATA1 =
            new ProfileDataSource.ProfileData(
                    /* accountName= */ "test.account1@gmail.com", /* avatar= */ null,
                    /* fullName= */ "Test Account1", /* givenName= */ "Account1");
    private static final ProfileDataSource.ProfileData PROFILE_DATA2 =
            new ProfileDataSource.ProfileData(
                    /* accountName= */ "test.account2@gmail.com", /* avatar= */ null,
                    /* fullName= */ null, /* givenName= */ null);

    @Rule
    public final RenderTestRule mRenderTestRule = RenderTestRule.Builder.withPublicCorpus().build();

    @Rule
    public final AccountManagerTestRule mAccountManagerTestRule =
            new AccountManagerTestRule(new FakeProfileDataSource());

    @Rule
    public final ChromeActivityTestRule<ChromeActivity> mActivityTestRule =
            new ChromeActivityTestRule<>(ChromeActivity.class);

    @Mock
    private AccountPickerDelegate mAccountPickerDelegateMock;

    private AccountPickerBottomSheetCoordinator mCoordinator;

    @ParameterAnnotations.UseMethodParameterBefore(NightModeTestUtils.NightModeParams.class)
    public void setupNightMode(boolean nightModeEnabled) {
        ChromeNightModeTestUtils.setUpNightModeForChromeActivity(nightModeEnabled);
        mRenderTestRule.setNightModeEnabled(nightModeEnabled);
    }

    @BeforeClass
    public static void setUpBeforeActivityLaunched() {
        ChromeNightModeTestUtils.setUpNightModeBeforeChromeActivityLaunched();
    }

    @Before
    public void setUp() {
        initMocks(this);
        mActivityTestRule.startMainActivityOnBlankPage();
    }

    @AfterClass
    public static void tearDownAfterActivityDestroyed() {
        ChromeNightModeTestUtils.tearDownNightModeAfterChromeActivityDestroyed();
    }

    @Test
    @MediumTest
    @Feature("RenderTest")
    @ParameterAnnotations.UseMethodParameter(NightModeTestUtils.NightModeParams.class)
    public void testCollapsedSheetWithAccountView(boolean nightModeEnabled) throws IOException {
        mAccountManagerTestRule.addAccount(PROFILE_DATA1);
        buildAndShowCollapsedBottomSheet();
        mRenderTestRule.render(
                mCoordinator.getBottomSheetViewForTesting(), "collapsed_sheet_with_account");
    }

    @Test
    @MediumTest
    @Feature("RenderTest")
    @ParameterAnnotations.UseMethodParameter(NightModeTestUtils.NightModeParams.class)
    public void testExpandedSheetView(boolean nightModeEnabled) throws IOException {
        mAccountManagerTestRule.addAccount(PROFILE_DATA1);
        mAccountManagerTestRule.addAccount(PROFILE_DATA2);
        buildAndShowCollapsedBottomSheet();
        onView(withText(PROFILE_DATA1.getFullName())).perform(click());
        mRenderTestRule.render(mCoordinator.getBottomSheetViewForTesting(), "expanded_sheet");
    }

    @Test
    @MediumTest
    @Feature("RenderTest")
    @ParameterAnnotations.UseMethodParameter(NightModeTestUtils.NightModeParams.class)
    public void testSignInInProgressView(boolean nightModeEnabled) throws IOException {
        mAccountManagerTestRule.addAccount(PROFILE_DATA1);
        buildAndShowCollapsedBottomSheet();
        View bottomSheetView = mCoordinator.getBottomSheetViewForTesting();
        TestThreadUtils.runOnUiThreadBlocking(() -> {
            bottomSheetView.findViewById(R.id.account_picker_continue_as_button).performClick();
        });
        CriteriaHelper.pollUiThread(
                bottomSheetView.findViewById(R.id.account_picker_signin_spinner_view)::isShown);
        // Currently the ProgressBar animation cannot be disabled on android-marshmallow-arm64-rel
        // bot with DisableAnimationsTestRule, we hide the ProgressBar manually here to enable
        // checks of other elements on the screen.
        // TODO(https://crbug.com/1115067): Delete this line and use DisableAnimationsTestRule
        //  once DisableAnimationsTestRule is fixed.
        TestThreadUtils.runOnUiThreadBlocking(() -> {
            bottomSheetView.findViewById(R.id.account_picker_signin_spinner_view)
                    .setVisibility(View.INVISIBLE);
        });
        mRenderTestRule.render(
                mCoordinator.getBottomSheetViewForTesting(), "signin_in_progress_sheet");
    }

    private void buildAndShowCollapsedBottomSheet() {
        TestThreadUtils.runOnUiThreadBlocking(() -> {
            mCoordinator = new AccountPickerBottomSheetCoordinator(mActivityTestRule.getActivity(),
                    getBottomSheetController(), mAccountPickerDelegateMock);
        });
        CriteriaHelper.pollUiThread(mCoordinator.getBottomSheetViewForTesting().findViewById(
                R.id.account_picker_continue_as_button)::isShown);
    }

    private BottomSheetController getBottomSheetController() {
        return mActivityTestRule.getActivity()
                .getRootUiCoordinatorForTesting()
                .getBottomSheetController();
    }
}
