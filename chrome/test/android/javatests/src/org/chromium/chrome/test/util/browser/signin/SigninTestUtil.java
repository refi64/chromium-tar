// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.chrome.test.util.browser.signin;

import android.accounts.Account;

import org.junit.Assert;

import org.chromium.base.ThreadUtils;
import org.chromium.base.test.util.CallbackHelper;
import org.chromium.chrome.browser.SyncFirstSetupCompleteSource;
import org.chromium.chrome.browser.profiles.Profile;
import org.chromium.chrome.browser.signin.IdentityServicesProvider;
import org.chromium.chrome.browser.signin.SigninManager;
import org.chromium.chrome.browser.sync.ProfileSyncService;
import org.chromium.components.signin.AccountTrackerService;
import org.chromium.components.signin.base.CoreAccountInfo;
import org.chromium.components.signin.identitymanager.ConsentLevel;
import org.chromium.components.signin.metrics.SigninAccessPoint;
import org.chromium.components.signin.metrics.SignoutReason;
import org.chromium.content_public.browser.test.util.TestThreadUtils;

import java.util.concurrent.TimeoutException;

/**
 * Utility class for test signin functionality.
 */
final class SigninTestUtil {
    /**
     * Returns the currently signed in account.
     */
    static Account getCurrentAccount() {
        return TestThreadUtils.runOnUiThreadBlockingNoException(() -> {
            return CoreAccountInfo.getAndroidAccountFrom(
                    IdentityServicesProvider.get()
                            .getIdentityManager(Profile.getLastUsedRegularProfile())
                            .getPrimaryAccountInfo(ConsentLevel.SYNC));
        });
    }

    /**
     * Sign into an account.
     */
    static void signIn(Account account) {
        CallbackHelper callbackHelper = new CallbackHelper();
        TestThreadUtils.runOnUiThreadBlocking(() -> {
            SigninManager signinManager = IdentityServicesProvider.get().getSigninManager(
                    Profile.getLastUsedRegularProfile());
            signinManager.onFirstRunCheckDone(); // Allow sign-in
            signinManager.signIn(
                    SigninAccessPoint.UNKNOWN, account, new SigninManager.SignInCallback() {
                        @Override
                        public void onSignInComplete() {
                            ProfileSyncService.get().setFirstSetupComplete(
                                    SyncFirstSetupCompleteSource.BASIC_FLOW);
                            callbackHelper.notifyCalled();
                        }

                        @Override
                        public void onSignInAborted() {
                            Assert.fail("Sign-in was aborted");
                        }
                    });
        });
        try {
            callbackHelper.waitForFirst();
        } catch (TimeoutException e) {
            throw new RuntimeException("Timed out waiting for callback", e);
        }
        TestThreadUtils.runOnUiThreadBlocking(() -> {
            Assert.assertEquals(account.name,
                    IdentityServicesProvider.get()
                            .getIdentityManager(Profile.getLastUsedRegularProfile())
                            .getPrimaryAccountInfo(ConsentLevel.SYNC)
                            .getEmail());
        });
    }

    /**
     * Waits for the AccountTrackerService to seed system accounts.
     */
    static void seedAccounts() {
        ThreadUtils.assertOnBackgroundThread();
        CallbackHelper ch = new CallbackHelper();
        TestThreadUtils.runOnUiThreadBlocking(() -> {
            AccountTrackerService accountTrackerService =
                    IdentityServicesProvider.get().getAccountTrackerService(
                            Profile.getLastUsedRegularProfile());
            if (accountTrackerService.checkAndSeedSystemAccounts()) {
                ch.notifyCalled();
            } else {
                AccountTrackerService.OnSystemAccountsSeededListener listener =
                        new AccountTrackerService.OnSystemAccountsSeededListener() {
                            @Override
                            public void onSystemAccountsSeedingComplete() {
                                accountTrackerService.removeSystemAccountsSeededListener(this);
                                ch.notifyCalled();
                            }
                        };
                accountTrackerService.addSystemAccountsSeededListener(listener);
            }
        });
        try {
            ch.waitForFirst("Timed out while waiting for system accounts to seed.");
        } catch (TimeoutException ex) {
            throw new RuntimeException("Timed out while waiting for system accounts to seed.");
        }
    }

    static void signOut() {
        ThreadUtils.assertOnBackgroundThread();
        CallbackHelper callbackHelper = new CallbackHelper();
        TestThreadUtils.runOnUiThreadBlocking(() -> {
            IdentityServicesProvider.get()
                    .getSigninManager(Profile.getLastUsedRegularProfile())
                    .signOut(SignoutReason.SIGNOUT_TEST, callbackHelper::notifyCalled, false);
        });
        try {
            callbackHelper.waitForFirst();
        } catch (TimeoutException e) {
            throw new RuntimeException("Timed out waiting for callback", e);
        }
    }

    private SigninTestUtil() {}
}
