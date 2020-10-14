// Copyright 2020 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.chrome.browser.signin.account_picker;

import android.accounts.Account;
import android.content.Context;
import android.text.TextUtils;

import androidx.annotation.Nullable;

import org.chromium.base.task.AsyncTask;
import org.chromium.chrome.R;
import org.chromium.chrome.browser.signin.ProfileDataCache;
import org.chromium.chrome.browser.signin.account_picker.AccountPickerBottomSheetProperties.AccountPickerBottomSheetState;
import org.chromium.components.signin.AccountManagerFacade;
import org.chromium.components.signin.AccountManagerFacadeProvider;
import org.chromium.components.signin.AccountUtils;
import org.chromium.components.signin.AccountsChangeObserver;
import org.chromium.components.signin.base.CoreAccountId;
import org.chromium.components.signin.base.CoreAccountInfo;
import org.chromium.components.signin.base.GoogleServiceAuthError;
import org.chromium.components.signin.base.GoogleServiceAuthError.State;
import org.chromium.ui.modelutil.PropertyModel;

import java.util.Collections;
import java.util.List;

/**
 * Mediator of the account picker bottom sheet in web sign-in flow.
 */
class AccountPickerBottomSheetMediator implements AccountPickerCoordinator.Listener {
    private final AccountPickerDelegate mAccountPickerDelegate;
    private final ProfileDataCache mProfileDataCache;
    private final PropertyModel mModel;

    private final ProfileDataCache.Observer mProfileDataSourceObserver =
            this::updateSelectedAccountData;
    private final AccountManagerFacade mAccountManagerFacade;
    private final AccountsChangeObserver mAccountsChangeObserver = this::onAccountListUpdated;
    private @Nullable String mSelectedAccountName;

    AccountPickerBottomSheetMediator(Context context, AccountPickerDelegate accountPickerDelegate) {
        mAccountPickerDelegate = accountPickerDelegate;
        mProfileDataCache = new ProfileDataCache(
                context, context.getResources().getDimensionPixelSize(R.dimen.user_picture_size));

        mModel = AccountPickerBottomSheetProperties.createModel(
                this::onSelectedAccountClicked, this::onContinueAsClicked);
        mProfileDataCache.addObserver(mProfileDataSourceObserver);

        mAccountManagerFacade = AccountManagerFacadeProvider.getInstance();
        mAccountManagerFacade.addObserver(mAccountsChangeObserver);
        onAccountListUpdated();
    }

    /**
     * Notifies that the user has selected an account.
     *
     * @param accountName The email of the selected account.
     * @param isDefaultAccount Whether the selected account is the first in the account list.
     *
     * TODO(https://crbug.com/1115965): Use CoreAccountInfo instead of account's email
     * as the first argument of the method.
     */
    @Override
    public void onAccountSelected(String accountName, boolean isDefaultAccount) {
        // Clicking on one account in the account list when the account list is expanded
        // will collapse it to the selected account
        mModel.set(AccountPickerBottomSheetProperties.ACCOUNT_PICKER_BOTTOM_SHEET_STATE,
                AccountPickerBottomSheetState.COLLAPSED_ACCOUNT_LIST);
        setSelectedAccountName(accountName);
    }

    /**
     * Notifies when the user clicked the "add account" button.
     */
    @Override
    public void addAccount() {
        mAccountPickerDelegate.addAccount(accountName -> onAccountSelected(accountName, false));
    }

    /**
     * Notifies when the user clicked the "Go Incognito mode" button.
     */
    @Override
    public void goIncognitoMode() {
        mModel.set(AccountPickerBottomSheetProperties.ACCOUNT_PICKER_BOTTOM_SHEET_STATE,
                AccountPickerBottomSheetState.INCOGNITO_INTERSTITIAL);
        mAccountPickerDelegate.goIncognitoMode();
    }

    PropertyModel getModel() {
        return mModel;
    }

    void destroy() {
        mAccountPickerDelegate.onDismiss();
        mProfileDataCache.removeObserver(mProfileDataSourceObserver);
        mAccountManagerFacade.removeObserver(mAccountsChangeObserver);
    }

    /**
     * Updates the collapsed account list when account list changes.
     *
     * Implements {@link AccountsChangeObserver}.
     */
    private void onAccountListUpdated() {
        List<Account> accounts = mAccountManagerFacade.tryGetGoogleAccounts();
        if (accounts.isEmpty()) {
            // If all accounts disappeared, no matter if the account list is collapsed or expanded,
            // we will go to the zero account screen.
            mModel.set(AccountPickerBottomSheetProperties.ACCOUNT_PICKER_BOTTOM_SHEET_STATE,
                    AccountPickerBottomSheetState.NO_ACCOUNTS);
            mSelectedAccountName = null;
            mModel.set(AccountPickerBottomSheetProperties.SELECTED_ACCOUNT_DATA, null);
            return;
        }

        @AccountPickerBottomSheetState
        int state =
                mModel.get(AccountPickerBottomSheetProperties.ACCOUNT_PICKER_BOTTOM_SHEET_STATE);
        if (state == AccountPickerBottomSheetState.NO_ACCOUNTS) {
            // When a non-empty account list appears while it is currently zero-account screen,
            // we should change the screen to collapsed account list and set the selected account
            // to the first account of the account list
            mModel.set(AccountPickerBottomSheetProperties.ACCOUNT_PICKER_BOTTOM_SHEET_STATE,
                    AccountPickerBottomSheetState.COLLAPSED_ACCOUNT_LIST);
            setSelectedAccountName(accounts.get(0).name);
        } else if (state == AccountPickerBottomSheetState.COLLAPSED_ACCOUNT_LIST
                && AccountUtils.findAccountByName(accounts, mSelectedAccountName) == null) {
            // When it is already collapsed account list, we update the selected account only
            // when the current selected account name is no longer in the new account list
            setSelectedAccountName(accounts.get(0).name);
        }
    }

    private void setSelectedAccountName(String accountName) {
        mSelectedAccountName = accountName;
        mProfileDataCache.update(Collections.singletonList(mSelectedAccountName));
        updateSelectedAccountData(mSelectedAccountName);
    }

    /**
     * Implements {@link ProfileDataCache.Observer}.
     */
    private void updateSelectedAccountData(String accountName) {
        if (TextUtils.equals(mSelectedAccountName, accountName)) {
            mModel.set(AccountPickerBottomSheetProperties.SELECTED_ACCOUNT_DATA,
                    mProfileDataCache.getProfileDataOrDefault(accountName));
        }
    }

    /**
     * Callback for the PropertyKey
     * {@link AccountPickerBottomSheetProperties#ON_SELECTED_ACCOUNT_CLICKED}.
     */
    private void onSelectedAccountClicked() {
        // Clicking on the selected account when the account list is collapsed will expand the
        // account list and make the account list visible
        mModel.set(AccountPickerBottomSheetProperties.ACCOUNT_PICKER_BOTTOM_SHEET_STATE,
                AccountPickerBottomSheetState.EXPANDED_ACCOUNT_LIST);
    }

    /**
     * Callback for the PropertyKey
     * {@link AccountPickerBottomSheetProperties#ON_CONTINUE_AS_CLICKED}.
     */
    private void onContinueAsClicked() {
        if (mSelectedAccountName == null) {
            addAccount();
        } else {
            mModel.set(AccountPickerBottomSheetProperties.ACCOUNT_PICKER_BOTTOM_SHEET_STATE,
                    AccountPickerBottomSheetState.SIGNIN_IN_PROGRESS);
            new AsyncTask<String>() {
                @Override
                protected String doInBackground() {
                    return mAccountManagerFacade.getAccountGaiaId(mSelectedAccountName);
                }

                @Override
                protected void onPostExecute(String accountGaiaId) {
                    CoreAccountInfo coreAccountInfo = new CoreAccountInfo(
                            new CoreAccountId(accountGaiaId), mSelectedAccountName, accountGaiaId);
                    mAccountPickerDelegate.signIn(
                            coreAccountInfo, AccountPickerBottomSheetMediator.this::onSignInError);
                }
            }.executeOnExecutor(AsyncTask.THREAD_POOL_EXECUTOR);
        }
    }

    private void onSignInError(GoogleServiceAuthError error) {
        if (error.getState() == State.INVALID_GAIA_CREDENTIALS) {
            mModel.set(AccountPickerBottomSheetProperties.ACCOUNT_PICKER_BOTTOM_SHEET_STATE,
                    AccountPickerBottomSheetState.SIGNIN_AUTH_ERROR);
        } else {
            mModel.set(AccountPickerBottomSheetProperties.ACCOUNT_PICKER_BOTTOM_SHEET_STATE,
                    AccountPickerBottomSheetState.SIGNIN_GENERAL_ERROR);
        }
    }
}
