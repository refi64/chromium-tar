// Copyright 2020 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.chrome.browser.signin;

import static androidx.test.espresso.Espresso.onView;
import static androidx.test.espresso.action.ViewActions.click;
import static androidx.test.espresso.action.ViewActions.pressBack;
import static androidx.test.espresso.assertion.ViewAssertions.doesNotExist;
import static androidx.test.espresso.assertion.ViewAssertions.matches;
import static androidx.test.espresso.matcher.ViewMatchers.Visibility.VISIBLE;
import static androidx.test.espresso.matcher.ViewMatchers.isDisplayed;
import static androidx.test.espresso.matcher.ViewMatchers.isRoot;
import static androidx.test.espresso.matcher.ViewMatchers.withEffectiveVisibility;
import static androidx.test.espresso.matcher.ViewMatchers.withId;
import static androidx.test.espresso.matcher.ViewMatchers.withText;

import static org.hamcrest.Matchers.not;
import static org.hamcrest.core.AllOf.allOf;
import static org.mockito.ArgumentMatchers.any;
import static org.mockito.ArgumentMatchers.eq;
import static org.mockito.ArgumentMatchers.notNull;
import static org.mockito.Mockito.doAnswer;
import static org.mockito.Mockito.verify;
import static org.mockito.MockitoAnnotations.initMocks;

import android.support.test.InstrumentationRegistry;
import android.view.View;

import androidx.test.filters.MediumTest;

import org.junit.Assert;
import org.junit.Before;
import org.junit.ClassRule;
import org.junit.Rule;
import org.junit.Test;
import org.junit.rules.RuleChain;
import org.junit.runner.RunWith;
import org.mockito.ArgumentCaptor;
import org.mockito.Captor;
import org.mockito.Mock;

import org.chromium.base.Callback;
import org.chromium.base.ThreadUtils;
import org.chromium.base.test.util.CommandLineFlags;
import org.chromium.chrome.R;
import org.chromium.chrome.browser.flags.ChromeFeatureList;
import org.chromium.chrome.browser.flags.ChromeSwitches;
import org.chromium.chrome.browser.signin.account_picker.AccountPickerBottomSheetCoordinator;
import org.chromium.chrome.browser.signin.account_picker.AccountPickerDelegate;
import org.chromium.chrome.test.ChromeJUnit4ClassRunner;
import org.chromium.chrome.test.ChromeTabbedActivityTestRule;
import org.chromium.chrome.test.util.browser.Features;
import org.chromium.chrome.test.util.browser.signin.AccountManagerTestRule;
import org.chromium.components.browser_ui.bottomsheet.BottomSheetController;
import org.chromium.components.signin.AccountManagerFacadeProvider;
import org.chromium.components.signin.ProfileDataSource;
import org.chromium.components.signin.base.CoreAccountInfo;
import org.chromium.components.signin.base.GoogleServiceAuthError;
import org.chromium.components.signin.base.GoogleServiceAuthError.State;
import org.chromium.components.signin.test.util.FakeAccountManagerFacade;
import org.chromium.components.signin.test.util.FakeProfileDataSource;
import org.chromium.content_public.browser.test.util.CriteriaHelper;
import org.chromium.content_public.browser.test.util.TestThreadUtils;
import org.chromium.ui.test.util.DisableAnimationsTestRule;

/**
 * Tests account picker bottom sheet of the web signin flow.
 */
@RunWith(ChromeJUnit4ClassRunner.class)
@CommandLineFlags.Add({ChromeSwitches.DISABLE_FIRST_RUN_EXPERIENCE})
@Features.EnableFeatures({ChromeFeatureList.MOBILE_IDENTITY_CONSISTENCY})
public class AccountPickerBottomSheetTest {
    private static class CustomFakeProfileDataSource extends FakeProfileDataSource {
        int getNumberOfObservers() {
            return mObservers.size();
        }
    }

    private static final ProfileDataSource.ProfileData PROFILE_DATA1 =
            new ProfileDataSource.ProfileData(
                    /* accountName= */ "test.account1@gmail.com", /* avatar= */ null,
                    /* fullName= */ "Test Account1", /* givenName= */ "Account1");
    private static final ProfileDataSource.ProfileData PROFILE_DATA2 =
            new ProfileDataSource.ProfileData(
                    /* accountName= */ "test.account2@gmail.com", /* avatar= */ null,
                    /* fullName= */ null, /* givenName= */ null);

    // Disable animations to reduce flakiness.
    @ClassRule
    public static final DisableAnimationsTestRule sNoAnimationsRule =
            new DisableAnimationsTestRule();

    @Captor
    public ArgumentCaptor<Callback<String>> callbackArgumentCaptor;

    private final ChromeTabbedActivityTestRule mActivityTestRule =
            new ChromeTabbedActivityTestRule();

    private final CustomFakeProfileDataSource mFakeProfileDataSource =
            new CustomFakeProfileDataSource();

    private final AccountManagerTestRule mAccountManagerTestRule =
            new AccountManagerTestRule(mFakeProfileDataSource);

    private AccountPickerBottomSheetCoordinator mCoordinator;

    // Destroys the mock AccountManagerFacade in the end as ChromeActivity may needs
    // to unregister observers in the stub.
    @Rule
    public final RuleChain mRuleChain =
            RuleChain.outerRule(mAccountManagerTestRule).around(mActivityTestRule);

    @Mock
    private AccountPickerDelegate mAccountPickerDelegateMock;

    @Before
    public void setUp() {
        initMocks(this);
        mAccountManagerTestRule.addAccount(PROFILE_DATA1);
        mAccountManagerTestRule.addAccount(PROFILE_DATA2);
        mActivityTestRule.startMainActivityOnBlankPage();
    }

    @Test
    @MediumTest
    public void testCollapsedSheetWithAccount() {
        buildAndShowCollapsedBottomSheet();
        checkCollapsedAccountList(PROFILE_DATA1);
    }

    @Test
    @MediumTest
    public void testExpandedSheet() {
        buildAndShowExpandedBottomSheet();
        onView(allOf(withText(PROFILE_DATA1.getAccountName()), withEffectiveVisibility(VISIBLE)))
                .check(matches(isDisplayed()));
        onView(allOf(withText(PROFILE_DATA1.getFullName()), withEffectiveVisibility(VISIBLE)))
                .check(matches(isDisplayed()));
        onView(withText(PROFILE_DATA2.getAccountName())).check(matches(isDisplayed()));
        onView(withText(R.string.signin_add_account_to_device)).check(matches(isDisplayed()));

        onView(withId(R.id.account_picker_selected_account)).check(matches(not(isDisplayed())));
        onView(withId(R.id.account_picker_continue_as_button)).check(matches(not(isDisplayed())));
    }

    @Test
    @MediumTest
    public void testCollapsedSheetWithZeroAccount() {
        // As we have already added accounts in our current AccountManagerFacade mock
        // Here since we want to test a zero account case, we would like to set up
        // a new AccountManagerFacade mock with no account in it. The mock will be
        // torn down in the end of the test in AccountManagerTestRule.
        AccountManagerFacadeProvider.setInstanceForTests(
                new FakeAccountManagerFacade(mFakeProfileDataSource));
        buildAndShowCollapsedBottomSheet();
        checkZeroAccountBottomSheet();
    }

    @Test
    @MediumTest
    public void testDismissCollapsedSheet() {
        buildAndShowCollapsedBottomSheet();
        onView(withText(PROFILE_DATA1.getAccountName())).check(matches(isDisplayed()));
        BottomSheetController controller = getBottomSheetController();
        Assert.assertTrue(controller.isSheetOpen());
        Assert.assertEquals(2, mFakeProfileDataSource.getNumberOfObservers());
        onView(isRoot()).perform(pressBack());
        Assert.assertFalse(controller.isSheetOpen());
        verify(mAccountPickerDelegateMock).onDismiss();
        Assert.assertEquals(0, mFakeProfileDataSource.getNumberOfObservers());
    }

    @Test
    @MediumTest
    public void testDismissExpandedSheet() {
        buildAndShowExpandedBottomSheet();
        BottomSheetController controller = getBottomSheetController();
        Assert.assertTrue(controller.isSheetOpen());
        Assert.assertEquals(2, mFakeProfileDataSource.getNumberOfObservers());
        onView(isRoot()).perform(pressBack());
        Assert.assertFalse(controller.isSheetOpen());
        verify(mAccountPickerDelegateMock).onDismiss();
        Assert.assertEquals(0, mFakeProfileDataSource.getNumberOfObservers());
    }

    @Test
    @MediumTest
    public void testAccountDisappearedOnCollapsedSheet() {
        buildAndShowCollapsedBottomSheet();
        mAccountManagerTestRule.removeAccountAndWaitForSeeding(PROFILE_DATA1.getAccountName());
        mAccountManagerTestRule.removeAccountAndWaitForSeeding(PROFILE_DATA2.getAccountName());
        InstrumentationRegistry.getInstrumentation().waitForIdleSync();
        checkZeroAccountBottomSheet();
    }

    @Test
    @MediumTest
    public void testAccountDisappearedOnExpandedSheet() {
        buildAndShowExpandedBottomSheet();
        mAccountManagerTestRule.removeAccountAndWaitForSeeding(PROFILE_DATA1.getAccountName());
        mAccountManagerTestRule.removeAccountAndWaitForSeeding(PROFILE_DATA2.getAccountName());
        InstrumentationRegistry.getInstrumentation().waitForIdleSync();
        checkZeroAccountBottomSheet();
    }

    @Test
    @MediumTest
    public void testAccountReappearedOnCollapsedSheet() {
        mAccountManagerTestRule.removeAccountAndWaitForSeeding(PROFILE_DATA1.getAccountName());
        mAccountManagerTestRule.removeAccountAndWaitForSeeding(PROFILE_DATA2.getAccountName());
        buildAndShowCollapsedBottomSheet();
        checkZeroAccountBottomSheet();

        mAccountManagerTestRule.addAccount(PROFILE_DATA1.getAccountName());
        InstrumentationRegistry.getInstrumentation().waitForIdleSync();
        checkCollapsedAccountList(PROFILE_DATA1);
    }

    @Test
    @MediumTest
    public void testOtherAccountsChangeOnCollapsedSheet() {
        buildAndShowCollapsedBottomSheet();
        checkCollapsedAccountList(PROFILE_DATA1);
        mAccountManagerTestRule.removeAccountAndWaitForSeeding(PROFILE_DATA2.getAccountName());
        InstrumentationRegistry.getInstrumentation().waitForIdleSync();
        checkCollapsedAccountList(PROFILE_DATA1);
    }

    @Test
    @MediumTest
    public void testSelectedAccountChangeOnCollapsedSheet() {
        buildAndShowCollapsedBottomSheet();
        mAccountManagerTestRule.removeAccountAndWaitForSeeding(PROFILE_DATA1.getAccountName());
        InstrumentationRegistry.getInstrumentation().waitForIdleSync();
        checkCollapsedAccountList(PROFILE_DATA2);
    }

    @Test
    @MediumTest
    public void testProfileDataUpdateOnExpandedSheet() {
        buildAndShowExpandedBottomSheet();
        String newFullName = "New Full Name1";
        String newGivenName = "New Given Name1";
        TestThreadUtils.runOnUiThreadBlocking(() -> {
            mFakeProfileDataSource.setProfileData(PROFILE_DATA1.getAccountName(),
                    new ProfileDataSource.ProfileData(
                            PROFILE_DATA1.getAccountName(), null, newFullName, newGivenName));
        });
        onView(allOf(withText(newFullName), withEffectiveVisibility(VISIBLE)))
                .check(matches(isDisplayed()));
        // Check that profile data update when the bottom sheet is expanded won't
        // toggle out any hidden part.
        onView(withId(R.id.account_picker_selected_account)).check(matches(not(isDisplayed())));
        onView(withId(R.id.account_picker_continue_as_button)).check(matches(not(isDisplayed())));
    }

    @Test
    @MediumTest
    public void testSignInDefaultAccountOnCollapsedSheet() {
        buildAndShowCollapsedBottomSheet();
        View bottomSheetView = mCoordinator.getBottomSheetViewForTesting();
        ThreadUtils.runOnUiThread(
                bottomSheetView.findViewById(R.id.account_picker_continue_as_button)::performClick);
        checkSignInInProgressBottomSheet();
    }

    @Test
    @MediumTest
    public void testSignInAnotherAccount() {
        buildAndShowExpandedBottomSheet();
        onView(withText(PROFILE_DATA2.getAccountName())).perform(click());
        View bottomSheetView = mCoordinator.getBottomSheetViewForTesting();
        CriteriaHelper.pollUiThread(
                bottomSheetView.findViewById(R.id.account_picker_continue_as_button)::isShown);
        ThreadUtils.runOnUiThread(
                bottomSheetView.findViewById(R.id.account_picker_continue_as_button)::performClick);
        checkSignInInProgressBottomSheet();
    }

    @Test
    @MediumTest
    public void testSignInGeneralError() {
        // Throws a connection error during the sign-in action
        doAnswer(invocation -> {
            Callback<GoogleServiceAuthError> onSignInErrorCallback = invocation.getArgument(1);
            onSignInErrorCallback.onResult(new GoogleServiceAuthError(State.CONNECTION_FAILED));
            return null;
        })
                .when(mAccountPickerDelegateMock)
                .signIn(eq(mAccountManagerTestRule.toCoreAccountInfo(
                                PROFILE_DATA1.getAccountName())),
                        any());

        buildAndShowCollapsedBottomSheet();
        View bottomSheetView = mCoordinator.getBottomSheetViewForTesting();
        ThreadUtils.runOnUiThread(
                bottomSheetView.findViewById(R.id.account_picker_continue_as_button)::performClick);
        CriteriaHelper.pollUiThread(() -> {
            return !bottomSheetView.findViewById(R.id.account_picker_selected_account).isShown()
                    && bottomSheetView.findViewById(R.id.account_picker_bottom_sheet_subtitle)
                               .isShown();
        });
        onView(withText(R.string.signin_account_picker_bottom_sheet_error_title))
                .check(matches(isDisplayed()));
        onView(withText(R.string.signin_account_picker_general_error_subtitle))
                .check(matches(isDisplayed()));
        onView(withText(R.string.signin_account_picker_general_error_button))
                .check(matches(isDisplayed()));
        onView(withId(R.id.account_picker_horizontal_divider)).check(matches(not(isDisplayed())));
        onView(withId(R.id.account_picker_selected_account)).check(matches(not(isDisplayed())));
        onView(withId(R.id.account_picker_signin_spinner_view)).check(matches(not(isDisplayed())));
    }

    @Test
    @MediumTest
    public void testSignInAuthError() {
        CoreAccountInfo coreAccountInfo =
                mAccountManagerTestRule.toCoreAccountInfo(PROFILE_DATA1.getAccountName());
        // Throws an auth error during the sign-in action
        doAnswer(invocation -> {
            Callback<GoogleServiceAuthError> onSignInErrorCallback = invocation.getArgument(1);
            onSignInErrorCallback.onResult(
                    new GoogleServiceAuthError(State.INVALID_GAIA_CREDENTIALS));
            return null;
        })
                .when(mAccountPickerDelegateMock)
                .signIn(eq(coreAccountInfo), any());

        buildAndShowCollapsedBottomSheet();
        View bottomSheetView = mCoordinator.getBottomSheetViewForTesting();
        ThreadUtils.runOnUiThread(
                bottomSheetView.findViewById(R.id.account_picker_continue_as_button)::performClick);
        CriteriaHelper.pollUiThread(() -> {
            return !bottomSheetView.findViewById(R.id.account_picker_selected_account).isShown()
                    && bottomSheetView.findViewById(R.id.account_picker_bottom_sheet_subtitle)
                               .isShown();
        });
        onView(withText(R.string.signin_account_picker_bottom_sheet_error_title))
                .check(matches(isDisplayed()));
        onView(withId(R.id.account_picker_horizontal_divider)).check(matches(not(isDisplayed())));
        onView(withId(R.id.account_picker_selected_account)).check(matches(not(isDisplayed())));
        onView(withId(R.id.account_picker_signin_spinner_view)).check(matches(not(isDisplayed())));
    }

    @Test
    @MediumTest
    public void testAddAccountOnExpandedSheet() {
        buildAndShowExpandedBottomSheet();
        onView(withText(R.string.signin_add_account_to_device)).perform(click());
        verify(mAccountPickerDelegateMock).addAccount(callbackArgumentCaptor.capture());
        ProfileDataSource.ProfileData profileDataAdded = new ProfileDataSource.ProfileData(
                /* accountName= */ "test.account3@gmail.com", /* avatar= */ null,
                /* fullName= */ null, /* givenName= */ null);
        Callback<String> callback = callbackArgumentCaptor.getValue();
        TestThreadUtils.runOnUiThreadBlocking(
                () -> callback.onResult(profileDataAdded.getAccountName()));
        CriteriaHelper.pollUiThread(mCoordinator.getBottomSheetViewForTesting().findViewById(
                R.id.account_picker_selected_account)::isShown);
        checkCollapsedAccountList(profileDataAdded);
    }

    @Test
    @MediumTest
    public void testSelectAnotherAccountOnExpandedSheet() {
        buildAndShowExpandedBottomSheet();
        onView(withText(PROFILE_DATA2.getAccountName())).perform(click());
        checkCollapsedAccountList(PROFILE_DATA2);
    }

    @Test
    @MediumTest
    public void testSelectTheSameAccountOnExpandedSheet() {
        buildAndShowExpandedBottomSheet();
        onView(allOf(withText(PROFILE_DATA1.getAccountName()), withEffectiveVisibility(VISIBLE)))
                .perform(click());
        checkCollapsedAccountList(PROFILE_DATA1);
    }

    @Test
    @MediumTest
    public void testIncognitoOptionShownOnExpandedSheet() {
        buildAndShowExpandedBottomSheet();
        onView(withText(R.string.signin_incognito_mode_secondary)).check(matches(isDisplayed()));
        onView(withText(R.string.signin_incognito_mode_primary)).perform(click());
        verify(mAccountPickerDelegateMock).goIncognitoMode();
        checkIncognitoInterstitialSheet();
    }

    private void checkIncognitoInterstitialSheet() {
        onView(withId(R.id.account_picker_bottom_sheet_logo)).check(matches(isDisplayed()));
        onView(withId(R.id.account_picker_bottom_sheet_title)).check(matches(not(isDisplayed())));
        onView(withId(R.id.account_picker_bottom_sheet_subtitle))
                .check(matches(not(isDisplayed())));
        onView(withId(R.id.account_picker_horizontal_divider)).check(matches(not(isDisplayed())));
        onView(withId(R.id.account_picker_account_list)).check(matches(not(isDisplayed())));

        onView(withId(R.id.incognito_interstitial_bottom_sheet_view)).check(matches(isDisplayed()));
    }

    private void checkSignInInProgressBottomSheet() {
        View bottomSheetView = mCoordinator.getBottomSheetViewForTesting();
        CriteriaHelper.pollUiThread(() -> {
            return !bottomSheetView.findViewById(R.id.account_picker_continue_as_button).isShown();
        });
        // TODO(https://crbug.com/1116348): Check AccountPickerDelegate.signIn() is called
        // after solving AsyncTask wait problem in espresso
        Assert.assertTrue(
                bottomSheetView.findViewById(R.id.account_picker_signin_spinner_view).isShown());
        // Currently the ProgressBar animation cannot be disabled on android-marshmallow-arm64-rel
        // bot with DisableAnimationsTestRule, we hide the ProgressBar manually here to enable
        // checks of other elements on the screen.
        // TODO(https://crbug.com/1115067): Delete this line once DisableAnimationsTestRule is
        // fixed.
        ThreadUtils.runOnUiThread(() -> {
            bottomSheetView.findViewById(R.id.account_picker_signin_spinner_view)
                    .setVisibility(View.GONE);
        });
        onView(withText(R.string.signin_account_picker_bottom_sheet_signin_title))
                .check(matches(isDisplayed()));
        onView(withId(R.id.account_picker_bottom_sheet_subtitle))
                .check(matches(not(isDisplayed())));
        onView(withId(R.id.account_picker_horizontal_divider)).check(matches(not(isDisplayed())));
        onView(withId(R.id.account_picker_account_list)).check(matches(not(isDisplayed())));
        onView(withId(R.id.account_picker_selected_account)).check(matches(not(isDisplayed())));
        onView(withId(R.id.account_picker_continue_as_button)).check(matches(not(isDisplayed())));
    }

    private void checkZeroAccountBottomSheet() {
        onView(allOf(withText(PROFILE_DATA1.getAccountName()), withEffectiveVisibility(VISIBLE)))
                .check(doesNotExist());
        onView(allOf(withText(PROFILE_DATA2.getAccountName()), withEffectiveVisibility(VISIBLE)))
                .check(doesNotExist());
        onView(withId(R.id.account_picker_account_list)).check(matches(not(isDisplayed())));
        onView(withId(R.id.account_picker_selected_account)).check(matches(not(isDisplayed())));
        onView(allOf(withText(R.string.signin_add_account_to_device),
                       withEffectiveVisibility(VISIBLE)))
                .perform(click());
        verify(mAccountPickerDelegateMock).addAccount(notNull());
    }

    private void checkCollapsedAccountList(ProfileDataSource.ProfileData profileData) {
        onView(withText(R.string.signin_account_picker_dialog_title)).check(matches(isDisplayed()));
        onView(allOf(withText(profileData.getAccountName()), withEffectiveVisibility(VISIBLE)))
                .check(matches(isDisplayed()));
        if (profileData.getFullName() != null) {
            onView(allOf(withText(profileData.getFullName()), withEffectiveVisibility(VISIBLE)))
                    .check(matches(isDisplayed()));
        }
        onView(allOf(withId(R.id.account_selection_mark), withEffectiveVisibility(VISIBLE)))
                .check(matches(isDisplayed()));
        String continueAsText =
                mActivityTestRule.getActivity().getString(R.string.signin_promo_continue_as,
                        profileData.getGivenName() != null ? profileData.getGivenName()
                                                           : profileData.getAccountName());
        onView(withText(continueAsText)).check(matches(isDisplayed()));
        onView(withId(R.id.account_picker_account_list)).check(matches(not(isDisplayed())));
    }

    private void buildAndShowCollapsedBottomSheet() {
        TestThreadUtils.runOnUiThreadBlocking(() -> {
            mCoordinator = new AccountPickerBottomSheetCoordinator(mActivityTestRule.getActivity(),
                    getBottomSheetController(), mAccountPickerDelegateMock);
        });
        CriteriaHelper.pollUiThread(mCoordinator.getBottomSheetViewForTesting().findViewById(
                R.id.account_picker_continue_as_button)::isShown);
    }

    private void buildAndShowExpandedBottomSheet() {
        buildAndShowCollapsedBottomSheet();
        onView(withText(PROFILE_DATA1.getFullName())).perform(click());
    }

    private BottomSheetController getBottomSheetController() {
        return mActivityTestRule.getActivity()
                .getRootUiCoordinatorForTesting()
                .getBottomSheetController();
    }
}
