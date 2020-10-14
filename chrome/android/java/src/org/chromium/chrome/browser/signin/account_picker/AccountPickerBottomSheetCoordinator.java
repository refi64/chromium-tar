// Copyright 2020 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.chrome.browser.signin.account_picker;

import android.content.Context;
import android.view.View;

import androidx.annotation.MainThread;
import androidx.annotation.VisibleForTesting;

import org.chromium.chrome.browser.signin.account_picker.AccountPickerCoordinator.AccountPickerAccessPoint;
import org.chromium.components.browser_ui.bottomsheet.BottomSheetController;
import org.chromium.components.browser_ui.bottomsheet.BottomSheetObserver;
import org.chromium.components.browser_ui.bottomsheet.EmptyBottomSheetObserver;
import org.chromium.ui.modelutil.PropertyModelChangeProcessor;

/**
 * Coordinator of the account picker bottom sheet used in web signin flow.
 */
public class AccountPickerBottomSheetCoordinator {
    private final AccountPickerBottomSheetView mView;
    private final AccountPickerBottomSheetMediator mAccountPickerBottomSheetMediator;
    private final AccountPickerCoordinator mAccountPickerCoordinator;
    private final BottomSheetController mBottomSheetController;
    private final BottomSheetObserver mBottomSheetObserver = new EmptyBottomSheetObserver() {
        @Override
        public void onSheetStateChanged(int newState) {
            super.onSheetStateChanged(newState);
            if (newState == BottomSheetController.SheetState.HIDDEN) {
                AccountPickerBottomSheetCoordinator.this.destroy();
            }
        }
    };

    /**
     * Constructs the AccountPickerBottomSheetCoordinator and shows the
     * bottom sheet on the screen.
     */
    @MainThread
    public AccountPickerBottomSheetCoordinator(Context context,
            BottomSheetController bottomSheetController,
            AccountPickerDelegate accountPickerDelegate) {
        mView = new AccountPickerBottomSheetView(context);
        mAccountPickerBottomSheetMediator =
                new AccountPickerBottomSheetMediator(context, accountPickerDelegate);
        mAccountPickerCoordinator = new AccountPickerCoordinator(mView.getAccountListView(),
                mAccountPickerBottomSheetMediator, null, AccountPickerAccessPoint.WEB);
        mBottomSheetController = bottomSheetController;
        PropertyModelChangeProcessor.create(mAccountPickerBottomSheetMediator.getModel(), mView,
                AccountPickerBottomSheetViewBinder::bind);
        mBottomSheetController.addObserver(mBottomSheetObserver);
        mBottomSheetController.requestShowContent(mView, true);
    }

    /**
     * Releases the resources used by AccountPickerBottomSheetCoordinator.
     */
    @MainThread
    private void destroy() {
        mAccountPickerCoordinator.destroy();
        mAccountPickerBottomSheetMediator.destroy();

        mBottomSheetController.removeObserver(mBottomSheetObserver);
    }

    @VisibleForTesting
    public View getBottomSheetViewForTesting() {
        return mView.getContentView();
    }
}
