// Copyright 2020 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.chrome.browser.payments.ui;

import android.content.Context;
import android.os.Handler;
import android.text.TextUtils;

import androidx.annotation.Nullable;
import androidx.annotation.VisibleForTesting;

import org.chromium.base.Callback;
import org.chromium.base.metrics.RecordHistogram;
import org.chromium.chrome.R;
import org.chromium.chrome.browser.app.ChromeActivity;
import org.chromium.chrome.browser.autofill.PersonalDataManager;
import org.chromium.chrome.browser.autofill.PersonalDataManager.AutofillProfile;
import org.chromium.chrome.browser.autofill.PersonalDataManager.CreditCard;
import org.chromium.chrome.browser.payments.AddressEditor;
import org.chromium.chrome.browser.payments.AutofillAddress;
import org.chromium.chrome.browser.payments.AutofillContact;
import org.chromium.chrome.browser.payments.AutofillPaymentAppCreator;
import org.chromium.chrome.browser.payments.AutofillPaymentAppFactory;
import org.chromium.chrome.browser.payments.AutofillPaymentInstrument;
import org.chromium.chrome.browser.payments.CardEditor;
import org.chromium.chrome.browser.payments.ContactEditor;
import org.chromium.chrome.browser.payments.PaymentRequestImpl;
import org.chromium.chrome.browser.payments.SettingsAutofillAndPaymentsObserver;
import org.chromium.chrome.browser.payments.handler.PaymentHandlerCoordinator;
import org.chromium.chrome.browser.payments.handler.PaymentHandlerCoordinator.PaymentHandlerUiObserver;
import org.chromium.chrome.browser.payments.handler.PaymentHandlerCoordinator.PaymentHandlerWebContentsObserver;
import org.chromium.chrome.browser.payments.ui.PaymentRequestSection.OptionSection.FocusChangedObserver;
import org.chromium.components.autofill.Completable;
import org.chromium.components.autofill.EditableOption;
import org.chromium.components.payments.CurrencyFormatter;
import org.chromium.components.payments.JourneyLogger;
import org.chromium.components.payments.PaymentApp;
import org.chromium.components.payments.PaymentAppType;
import org.chromium.components.payments.PaymentDetailsUpdateServiceHelper;
import org.chromium.components.payments.PaymentFeatureList;
import org.chromium.components.payments.PaymentOptionsUtils;
import org.chromium.components.payments.PaymentRequestLifecycleObserver;
import org.chromium.components.payments.PaymentRequestParams;
import org.chromium.components.payments.Section;
import org.chromium.content_public.browser.WebContents;
import org.chromium.payments.mojom.AddressErrors;
import org.chromium.payments.mojom.PayerDetail;
import org.chromium.payments.mojom.PayerErrors;
import org.chromium.payments.mojom.PaymentCurrencyAmount;
import org.chromium.payments.mojom.PaymentDetails;
import org.chromium.payments.mojom.PaymentDetailsModifier;
import org.chromium.payments.mojom.PaymentItem;
import org.chromium.payments.mojom.PaymentOptions;
import org.chromium.payments.mojom.PaymentShippingOption;
import org.chromium.payments.mojom.PaymentValidationErrors;
import org.chromium.url.GURL;

import java.util.ArrayList;
import java.util.Arrays;
import java.util.Collections;
import java.util.Comparator;
import java.util.HashMap;
import java.util.HashSet;
import java.util.LinkedList;
import java.util.List;
import java.util.Locale;
import java.util.Map;
import java.util.Queue;
import java.util.Set;

/**
 * This class manages all of the UIs related to payment. The UI logic of {@link PaymentRequestImpl}
 * should be moved into this class.
 */
public class PaymentUIsManager implements SettingsAutofillAndPaymentsObserver.Observer,
                                          PaymentRequestLifecycleObserver, PaymentHandlerUiObserver,
                                          FocusChangedObserver {
    /** Limit in the number of suggested items in a section. */
    /* package */ static final int SUGGESTIONS_LIMIT = 4;

    // Reverse order of the comparator to sort in descending order of completeness scores.
    private static final Comparator<Completable> COMPLETENESS_COMPARATOR =
            (a, b) -> (PaymentAppComparator.compareCompletablesByCompleteness(b, a));
    private final Comparator<PaymentApp> mPaymentAppComparator;

    private final boolean mIsOffTheRecord;
    private final Handler mHandler = new Handler();
    private final Queue<Runnable> mRetryQueue = new LinkedList<>();
    private ContactEditor mContactEditor;
    private PaymentHandlerCoordinator mPaymentHandlerUi;
    private Callback<PaymentInformation> mPaymentInformationCallback;
    private SectionInformation mUiShippingOptions;
    private final Delegate mDelegate;
    private final WebContents mWebContents;
    private final Map<String, CurrencyFormatter> mCurrencyFormatterMap;
    private final AddressEditor mAddressEditor;
    private final CardEditor mCardEditor;
    private final PaymentUisShowStateReconciler mPaymentUisShowStateReconciler;
    private final PaymentRequestParams mParams;

    private PaymentRequestUI mPaymentRequestUI;

    private ShoppingCart mUiShoppingCart;
    private Boolean mMerchantSupportsAutofillCards;
    private SectionInformation mPaymentMethodsSection;
    private SectionInformation mShippingAddressesSection;
    private ContactDetailsSection mContactSection;
    private AutofillPaymentAppCreator mAutofillPaymentAppCreator;
    private boolean mHaveRequestedAutofillData = true;
    private List<AutofillProfile> mAutofillProfiles;
    private Boolean mCanUserAddCreditCard;
    private final JourneyLogger mJourneyLogger;

    /** The delegate of this class. */
    public interface Delegate {
        /** Dispatch the payer detail change event if needed. */
        void dispatchPayerDetailChangeEventIfNeeded(PayerDetail detail);
        /** Record the show event to the journey logger and record the transaction amount. */
        void recordShowEventAndTransactionAmount();
        /** Start the normalization of the new shipping address. */
        void startShippingAddressChangeNormalization(AutofillAddress editedAddress);
    }

    /**
     * This class is to coordinate the show state of a bottom sheet UI (either expandable payment
     * handler or minimal UI) and the Payment Request UI so that these visibility rules are
     * enforced:
     * 1. At most one UI is shown at any moment in case the Payment Request UI obstructs the bottom
     * sheet.
     * 2. Bottom sheet is prioritized to show over Payment Request UI
     */
    public class PaymentUisShowStateReconciler {
        // Whether the bottom sheet is showing.
        private boolean mShowingBottomSheet;
        // Whether to show the Payment Request UI when the bottom sheet is not being shown.
        private boolean mShouldShowDialog;

        /**
         * Show the Payment Request UI dialog when the bottom sheet is hidden, i.e., if the bottom
         * sheet hidden, show the dialog immediately; otherwise, show the dialog after the bottom
         * sheet hides.
         */
        /* package */ void showPaymentRequestDialogWhenNoBottomSheet() {
            mShouldShowDialog = true;
            updatePaymentRequestDialogShowState();
        }

        /** Hide the Payment Request UI dialog. */
        /* package */ void hidePaymentRequestDialog() {
            mShouldShowDialog = false;
            updatePaymentRequestDialogShowState();
        }

        /** A callback invoked when the Payment Request UI is closed. */
        public void onPaymentRequestUiClosed() {
            assert mPaymentRequestUI == null;
            mShouldShowDialog = false;
        }

        /** A callback invoked when the bottom sheet is shown, to enforce the visibility rules. */
        public void onBottomSheetShown() {
            mShowingBottomSheet = true;
            updatePaymentRequestDialogShowState();
        }

        /** A callback invoked when the bottom sheet is hidden, to enforce the visibility rules. */
        /* package */ void onBottomSheetClosed() {
            mShowingBottomSheet = false;
            updatePaymentRequestDialogShowState();
        }

        private void updatePaymentRequestDialogShowState() {
            if (mPaymentRequestUI == null) return;
            mPaymentRequestUI.setVisible(!mShowingBottomSheet && mShouldShowDialog);
        }
    }

    /**
     * Create PaymentUIsManager.
     * @param delegate The delegate of this instance.
     * @param params The parameters of the payment request specified by the merchant.
     * @param webContents The WebContents of the merchant page.
     * @param isOffTheRecord Whether merchant page is in an isOffTheRecord tab.
     * @param journeyLogger The logger of the user journey.
     */
    public PaymentUIsManager(Delegate delegate, PaymentRequestParams params,
            WebContents webContents, boolean isOffTheRecord, JourneyLogger journeyLogger) {
        mDelegate = delegate;
        mParams = params;

        // Do not persist changes on disk in OffTheRecord mode.
        mAddressEditor = new AddressEditor(
                AddressEditor.Purpose.PAYMENT_REQUEST, /*saveToDisk=*/!isOffTheRecord);
        // PaymentRequest card editor does not show the organization name in the dropdown with the
        // billing address labels.
        mCardEditor = new CardEditor(webContents, mAddressEditor, /*includeOrgLabel=*/false);
        mJourneyLogger = journeyLogger;
        mWebContents = webContents;

        mPaymentUisShowStateReconciler = new PaymentUisShowStateReconciler();
        mCurrencyFormatterMap = new HashMap<>();
        mIsOffTheRecord = isOffTheRecord;
        mPaymentAppComparator = new PaymentAppComparator(/*params=*/mParams);
    }

    /** @return The PaymentRequestUI. */
    public PaymentRequestUI getPaymentRequestUI() {
        return mPaymentRequestUI;
    }

    /**
     * Set the PaymentRequestUI.
     * @param paymentRequestUI The PaymentRequestUI.
     */
    public void setPaymentRequestUI(PaymentRequestUI paymentRequestUI) {
        mPaymentRequestUI = paymentRequestUI;
    }

    /** @return The PaymentUisShowStateReconciler. */
    public PaymentUisShowStateReconciler getPaymentUisShowStateReconciler() {
        return mPaymentUisShowStateReconciler;
    }

    /** @return Get the AddressEditor of the PaymentRequest UI. */
    public AddressEditor getAddressEditor() {
        return mAddressEditor;
    }

    /** @return Get the CardEditor of the PaymentRequest UI. */
    public CardEditor getCardEditor() {
        return mCardEditor;
    }

    /** @return Whether the merchant supports autofill cards. */
    @Nullable
    public Boolean merchantSupportsAutofillCards() {
        // TODO(crbug.com/1107039): this value should be asserted not null to avoid being used
        // before defined, after this bug is fixed.
        return mMerchantSupportsAutofillCards;
    }

    /** @return Get the PaymentMethodsSection of the PaymentRequest UI. */
    public SectionInformation getPaymentMethodsSection() {
        return mPaymentMethodsSection;
    }

    /** Set the PaymentMethodsSection of the PaymentRequest UI. */
    public void setPaymentMethodsSection(SectionInformation paymentMethodsSection) {
        mPaymentMethodsSection = paymentMethodsSection;
    }

    /** Get the ShippingAddressesSection of the PaymentRequest UI. */
    public SectionInformation getShippingAddressesSection() {
        return mShippingAddressesSection;
    }

    /** Set the ShippingAddressesSection of the PaymentRequest UI. */
    public void setShippingAddressesSection(SectionInformation shippingAddressesSection) {
        mShippingAddressesSection = shippingAddressesSection;
    }

    /** Get the ContactSection of the PaymentRequest UI. */
    public ContactDetailsSection getContactSection() {
        return mContactSection;
    }

    /** Set the ContactSection of the PaymentRequest UI. */
    public void setContactSection(ContactDetailsSection contactSection) {
        mContactSection = contactSection;
    }

    /** Set the AutofillPaymentAppCreator. */
    public void setAutofillPaymentAppCreator(AutofillPaymentAppCreator autofillPaymentAppCreator) {
        mAutofillPaymentAppCreator = autofillPaymentAppCreator;
    }

    /** @return Whether user can add credit card. */
    public boolean canUserAddCreditCard() {
        assert mCanUserAddCreditCard != null;
        return mCanUserAddCreditCard;
    }

    /**
     * The UI model of the shopping cart, including the total. Each item includes a label and a
     * price string. This data is passed to the UI.
     */
    public ShoppingCart getUiShoppingCart() {
        return mUiShoppingCart;
    }

    /** Set the shopping cart on the PaymentRequest UI. */
    public void setUiShoppingCart(ShoppingCart uiShoppingCart) {
        mUiShoppingCart = uiShoppingCart;
    }

    /** @return Get a map of currency code to CurrencyFormatter. */
    public Map<String, CurrencyFormatter> getCurrencyFormatterMap() {
        return mCurrencyFormatterMap;
    }

    /**
     * The UI model for the shipping options. Includes the label and sublabel for each shipping
     * option. Also keeps track of the selected shipping option. This data is passed to the UI.
     */
    public SectionInformation getUiShippingOptions() {
        return mUiShippingOptions;
    }

    /**
     * Set the shipping options for the Payment Request UI.
     * @param uiShippingOptions A shipping options to be displayed on the Payment Request UI.
     */
    public void setUiShippingOptions(SectionInformation uiShippingOptions) {
        mUiShippingOptions = uiShippingOptions;
    }

    /**
     * Set the call back of PaymentInformation. This callback would be invoked when the payment
     * information is retrieved.
     */
    public void setPaymentInformationCallback(
            Callback<PaymentInformation> paymentInformationCallback) {
        mPaymentInformationCallback = paymentInformationCallback;
    }

    /** Get the contact editor on PaymentRequest UI. */
    public ContactEditor getContactEditor() {
        return mContactEditor;
    }

    /** Get the retry queue. */
    public Queue<Runnable> getRetryQueue() {
        return mRetryQueue;
    }

    /** @return The autofill profiles. */
    public List<AutofillProfile> getAutofillProfiles() {
        return mAutofillProfiles;
    }

    /** @return Whether PaymentRequestUI has requested autofill data. */
    public boolean haveRequestedAutofillData() {
        return mHaveRequestedAutofillData;
    }

    // Implement SettingsAutofillAndPaymentsObserver.Observer:
    @Override
    public void onAddressUpdated(AutofillAddress address) {
        address.setShippingAddressLabelWithCountry();
        mCardEditor.updateBillingAddressIfComplete(address);

        if (mShippingAddressesSection != null) {
            mShippingAddressesSection.addAndSelectOrUpdateItem(address);
            mPaymentRequestUI.updateSection(
                    PaymentRequestUI.DataType.SHIPPING_ADDRESSES, mShippingAddressesSection);
        }

        if (mContactSection != null) {
            mContactSection.addOrUpdateWithAutofillAddress(address);
            mPaymentRequestUI.updateSection(
                    PaymentRequestUI.DataType.CONTACT_DETAILS, mContactSection);
        }
    }

    // Implement SettingsAutofillAndPaymentsObserver.Observer:
    @Override
    public void onAddressDeleted(String guid) {
        // TODO: Delete the address from getShippingAddressesSection() and
        // getContactSection(). Note that we only displayed
        // SUGGESTIONS_LIMIT addresses, so we may want to add back previously ignored addresses.
    }

    // Implement SettingsAutofillAndPaymentsObserver.Observer:
    @Override
    public void onCreditCardUpdated(CreditCard card) {
        assert mMerchantSupportsAutofillCards != null;
        if (!mMerchantSupportsAutofillCards || mPaymentMethodsSection == null
                || mAutofillPaymentAppCreator == null) {
            return;
        }

        PaymentApp updatedAutofillCard = mAutofillPaymentAppCreator.createPaymentAppForCard(card);

        // Can be null when the card added through settings does not match the requested card
        // network or is invalid, because autofill settings do not perform the same level of
        // validation as Basic Card implementation in Chrome.
        if (updatedAutofillCard == null) return;

        mPaymentMethodsSection.addAndSelectOrUpdateItem(updatedAutofillCard);

        updateAppModifiedTotals();

        if (mPaymentRequestUI != null) {
            mPaymentRequestUI.updateSection(
                    PaymentRequestUI.DataType.PAYMENT_METHODS, mPaymentMethodsSection);
        }
    }

    // Implement SettingsAutofillAndPaymentsObserver.Observer:
    @Override
    public void onCreditCardDeleted(String guid) {
        assert mMerchantSupportsAutofillCards != null;
        if (!mMerchantSupportsAutofillCards || mPaymentMethodsSection == null) return;

        mPaymentMethodsSection.removeAndUnselectItem(guid);

        updateAppModifiedTotals();

        if (mPaymentRequestUI != null) {
            mPaymentRequestUI.updateSection(
                    PaymentRequestUI.DataType.PAYMENT_METHODS, mPaymentMethodsSection);
        }
    }

    // Implement PaymentRequestLifecycleObserver:
    @Override
    public void onPaymentRequestParamsInitiated(PaymentRequestParams params) {
        // Checks whether the merchant supports autofill cards before show is called.
        mMerchantSupportsAutofillCards =
                AutofillPaymentAppFactory.merchantSupportsBasicCard(params.getMethodData());

        // If in strict mode, don't give user an option to add an autofill card during the checkout
        // to avoid the "unhappy" basic-card flow.
        mCanUserAddCreditCard = mMerchantSupportsAutofillCards
                && !PaymentFeatureList.isEnabledOrExperimentalFeaturesEnabled(
                        PaymentFeatureList.STRICT_HAS_ENROLLED_AUTOFILL_INSTRUMENT);

        if (PaymentOptionsUtils.requestAnyInformation(mParams.getPaymentOptions())) {
            mAutofillProfiles = Collections.unmodifiableList(
                    PersonalDataManager.getInstance().getProfilesToSuggest(
                            false /* includeNameInLabel */));
        }

        if (PaymentOptionsUtils.requestShipping(mParams.getPaymentOptions())) {
            boolean haveCompleteShippingAddress = false;
            for (int i = 0; i < mAutofillProfiles.size(); i++) {
                if (AutofillAddress.checkAddressCompletionStatus(
                            mAutofillProfiles.get(i), AutofillAddress.CompletenessCheckType.NORMAL)
                        == AutofillAddress.CompletionStatus.COMPLETE) {
                    haveCompleteShippingAddress = true;
                    break;
                }
            }
            mHaveRequestedAutofillData &= haveCompleteShippingAddress;
        }

        PaymentOptions options = mParams.getPaymentOptions();
        if (PaymentOptionsUtils.requestAnyContactInformation(mParams.getPaymentOptions())) {
            // Do not persist changes on disk in OffTheRecord mode.
            mContactEditor = new ContactEditor(PaymentOptionsUtils.requestPayerName(options),
                    PaymentOptionsUtils.requestPayerPhone(options),
                    PaymentOptionsUtils.requestPayerEmail(options),
                    /*saveToDisk=*/!mIsOffTheRecord);
            boolean haveCompleteContactInfo = false;
            for (int i = 0; i < getAutofillProfiles().size(); i++) {
                AutofillProfile profile = getAutofillProfiles().get(i);
                if (getContactEditor().checkContactCompletionStatus(profile.getFullName(),
                            profile.getPhoneNumber(), profile.getEmailAddress())
                        == ContactEditor.COMPLETE) {
                    haveCompleteContactInfo = true;
                    break;
                }
            }
            mHaveRequestedAutofillData &= haveCompleteContactInfo;
        }
    }

    // Implement PaymentRequestLifecycleObserver:
    @Override
    public void onRetry(PaymentValidationErrors errors) {
        // Remove all payment apps except the selected one.
        assert mPaymentMethodsSection != null;
        PaymentApp selectedApp = (PaymentApp) mPaymentMethodsSection.getSelectedItem();
        assert selectedApp != null;
        mPaymentMethodsSection = new SectionInformation(PaymentRequestUI.DataType.PAYMENT_METHODS,
                /* selection = */ 0, new ArrayList<>(Arrays.asList(selectedApp)));
        mPaymentRequestUI.updateSection(
                PaymentRequestUI.DataType.PAYMENT_METHODS, mPaymentMethodsSection);
        mPaymentRequestUI.disableAddingNewCardsDuringRetry();

        // Go back to the payment sheet
        mPaymentRequestUI.onPayButtonProcessingCancelled();
        PaymentDetailsUpdateServiceHelper.getInstance().reset();
        if (!TextUtils.isEmpty(errors.error)) {
            mPaymentRequestUI.setRetryErrorMessage(errors.error);
        } else {
            ChromeActivity activity = ChromeActivity.fromWebContents(mWebContents);
            mPaymentRequestUI.setRetryErrorMessage(
                    activity.getResources().getString(R.string.payments_error_message));
        }

        if (shouldShowShippingSection() && hasShippingAddressError(errors.shippingAddress)) {
            mRetryQueue.add(() -> {
                mAddressEditor.setAddressErrors(errors.shippingAddress);
                AutofillAddress selectedAddress =
                        (AutofillAddress) mShippingAddressesSection.getSelectedItem();
                // Log the edit of a shipping address.
                mJourneyLogger.incrementSelectionEdits(Section.SHIPPING_ADDRESS);
                editAddress(selectedAddress);
            });
        }

        if (shouldShowContactSection() && hasPayerError(errors.payer)) {
            mRetryQueue.add(() -> {
                mContactEditor.setPayerErrors(errors.payer);
                AutofillContact selectedContact =
                        (AutofillContact) mContactSection.getSelectedItem();
                mJourneyLogger.incrementSelectionEdits(Section.CONTACT_INFO);
                editContactOnPaymentRequestUI(selectedContact);
            });
        }

        if (!mRetryQueue.isEmpty()) mHandler.post(mRetryQueue.remove());
    }

    private boolean hasShippingAddressError(AddressErrors errors) {
        return !TextUtils.isEmpty(errors.addressLine) || !TextUtils.isEmpty(errors.city)
                || !TextUtils.isEmpty(errors.country)
                || !TextUtils.isEmpty(errors.dependentLocality)
                || !TextUtils.isEmpty(errors.organization) || !TextUtils.isEmpty(errors.phone)
                || !TextUtils.isEmpty(errors.postalCode) || !TextUtils.isEmpty(errors.recipient)
                || !TextUtils.isEmpty(errors.region) || !TextUtils.isEmpty(errors.sortingCode);
    }

    private boolean hasPayerError(PayerErrors errors) {
        return !TextUtils.isEmpty(errors.name) || !TextUtils.isEmpty(errors.phone)
                || !TextUtils.isEmpty(errors.email);
    }

    /** @return The selected payment app type. */
    public @PaymentAppType int getSelectedPaymentAppType() {
        return mPaymentMethodsSection != null && mPaymentMethodsSection.getSelectedItem() != null
                ? ((PaymentApp) mPaymentMethodsSection.getSelectedItem()).getPaymentAppType()
                : PaymentAppType.UNDEFINED;
    }

    /** Sets the modifier for the order summary based on the given app, if any. */
    public void updateOrderSummary(@Nullable PaymentApp app) {
        if (!PaymentFeatureList.isEnabled(PaymentFeatureList.WEB_PAYMENTS_MODIFIERS)) return;

        PaymentDetailsModifier modifier = getModifier(app);
        PaymentItem total = modifier == null ? null : modifier.total;
        if (total == null) total = mParams.getRawTotal();

        CurrencyFormatter formatter = getOrCreateCurrencyFormatter(total.amount);
        mUiShoppingCart.setTotal(new LineItem(total.label, formatter.getFormattedCurrencyCode(),
                formatter.format(total.amount.value), false /* isPending */));
        mUiShoppingCart.setAdditionalContents(modifier == null
                        ? null
                        : getLineItems(Arrays.asList(modifier.additionalDisplayItems)));
        if (mPaymentRequestUI != null) {
            mPaymentRequestUI.updateOrderSummarySection(mUiShoppingCart);
        }
    }

    /** @return The first modifier that matches the given app, or null. */
    @Nullable
    private PaymentDetailsModifier getModifier(@Nullable PaymentApp app) {
        Map<String, PaymentDetailsModifier> modifiers = mParams.getUnmodifiableModifiers();
        if (modifiers.isEmpty() || app == null) return null;
        // Makes a copy to ensure it is modifiable.
        Set<String> methodNames = new HashSet<>(app.getInstrumentMethodNames());
        methodNames.retainAll(modifiers.keySet());
        if (methodNames.isEmpty()) return null;

        for (String methodName : methodNames) {
            PaymentDetailsModifier modifier = modifiers.get(methodName);
            if (app.isValidForPaymentMethodData(methodName, modifier.methodData)) {
                return modifier;
            }
        }

        return null;
    }

    /** Updates the modifiers for payment apps and order summary. */
    public void updateAppModifiedTotals() {
        if (!PaymentFeatureList.isEnabled(PaymentFeatureList.WEB_PAYMENTS_MODIFIERS)) return;
        if (mParams.getMethodData().isEmpty()) return;
        if (mPaymentMethodsSection == null) return;

        for (int i = 0; i < mPaymentMethodsSection.getSize(); i++) {
            PaymentApp app = (PaymentApp) mPaymentMethodsSection.getItem(i);
            PaymentDetailsModifier modifier = getModifier(app);
            app.setModifiedTotal(modifier == null || modifier.total == null
                            ? null
                            : getOrCreateCurrencyFormatter(modifier.total.amount)
                                      .format(modifier.total.amount.value));
        }

        updateOrderSummary((PaymentApp) mPaymentMethodsSection.getSelectedItem());
    }

    /**
     * Gets currency formatter for a given PaymentCurrencyAmount,
     * creates one if no existing instance is found.
     *
     * @param amount The given payment amount.
     */
    public CurrencyFormatter getOrCreateCurrencyFormatter(PaymentCurrencyAmount amount) {
        String key = amount.currency;
        CurrencyFormatter formatter = mCurrencyFormatterMap.get(key);
        if (formatter == null) {
            formatter = new CurrencyFormatter(amount.currency, Locale.getDefault());
            mCurrencyFormatterMap.put(key, formatter);
        }
        return formatter;
    }

    /**
     * Converts a list of payment items and returns their parsed representation.
     *
     * @param items The payment items to parse. Can be null.
     * @return A list of valid line items.
     */
    public List<LineItem> getLineItems(@Nullable List<PaymentItem> items) {
        // Line items are optional.
        if (items == null) return new ArrayList<>();

        List<LineItem> result = new ArrayList<>(items.size());
        for (int i = 0; i < items.size(); i++) {
            PaymentItem item = items.get(i);
            CurrencyFormatter formatter = getOrCreateCurrencyFormatter(item.amount);
            result.add(new LineItem(item.label,
                    isMixedOrChangedCurrency() ? formatter.getFormattedCurrencyCode() : "",
                    formatter.format(item.amount.value), item.pending));
        }

        return Collections.unmodifiableList(result);
    }

    private boolean isMixedOrChangedCurrency() {
        return mCurrencyFormatterMap.size() > 1;
    }

    /**
     * Load required currency formatter for a given PaymentDetails.
     *
     * Note that the cache (mCurrencyFormatterMap) is not cleared for
     * updated payment details so as to indicate the currency has been changed.
     *
     * @param details The given payment details.
     */
    public void loadCurrencyFormattersForPaymentDetails(PaymentDetails details) {
        if (details.total != null) {
            getOrCreateCurrencyFormatter(details.total.amount);
        }

        if (details.displayItems != null) {
            for (PaymentItem item : details.displayItems) {
                getOrCreateCurrencyFormatter(item.amount);
            }
        }

        if (details.shippingOptions != null) {
            for (PaymentShippingOption option : details.shippingOptions) {
                getOrCreateCurrencyFormatter(option.amount);
            }
        }

        if (details.modifiers != null) {
            for (PaymentDetailsModifier modifier : details.modifiers) {
                if (modifier.total != null) getOrCreateCurrencyFormatter(modifier.total.amount);
                for (PaymentItem displayItem : modifier.additionalDisplayItems) {
                    getOrCreateCurrencyFormatter(displayItem.amount);
                }
            }
        }
    }

    /**
     * Converts a list of shipping options and returns their parsed representation.
     *
     * @param options The raw shipping options to parse. Can be null.
     * @return The UI representation of the shipping options.
     */
    public SectionInformation getShippingOptions(@Nullable PaymentShippingOption[] options) {
        // Shipping options are optional.
        if (options == null || options.length == 0) {
            return new SectionInformation(PaymentRequestUI.DataType.SHIPPING_OPTIONS);
        }

        List<EditableOption> result = new ArrayList<>();
        int selectedItemIndex = SectionInformation.NO_SELECTION;
        for (int i = 0; i < options.length; i++) {
            PaymentShippingOption option = options[i];
            CurrencyFormatter formatter = getOrCreateCurrencyFormatter(option.amount);
            String currencyPrefix = isMixedOrChangedCurrency()
                    ? formatter.getFormattedCurrencyCode() + "\u0020"
                    : "";
            result.add(new EditableOption(option.id, option.label,
                    currencyPrefix + formatter.format(option.amount.value), null));
            if (option.selected) selectedItemIndex = i;
        }

        return new SectionInformation(PaymentRequestUI.DataType.SHIPPING_OPTIONS, selectedItemIndex,
                Collections.unmodifiableList(result));
    }

    /** Destroy the currency formatters. */
    public void destroyCurrencyFormatters() {
        for (CurrencyFormatter formatter : mCurrencyFormatterMap.values()) {
            assert formatter != null;
            // Ensures the native implementation of currency formatter does not leak.
            formatter.destroy();
        }
        mCurrencyFormatterMap.clear();
    }

    /**
     * Notifies the UI about the changes in selected payment method.
     */
    public void onSelectedPaymentMethodUpdated() {
        mPaymentRequestUI.selectedPaymentMethodUpdated(
                new PaymentInformation(mUiShoppingCart, mShippingAddressesSection,
                        mUiShippingOptions, mContactSection, mPaymentMethodsSection));
    }

    /**
     * Update Payment Request UI with the update event's information and enable the UI. This method
     * should be called when the user interface is disabled with a "↻" spinner being displayed. The
     * user is unable to interact with the user interface until this method is called.
     * @return Whether this is the first time that payment information has been provided to the user
     *         interface, which indicates that the "UI shown" event should be recorded now.
     */
    public boolean enableAndUpdatePaymentRequestUIWithPaymentInfo() {
        boolean isFirstUpdate = false;
        if (mPaymentInformationCallback != null && mPaymentMethodsSection != null) {
            providePaymentInformationToPaymentRequestUI();
            isFirstUpdate = true;
        } else {
            mPaymentRequestUI.updateOrderSummarySection(mUiShoppingCart);
            if (shouldShowShippingSection()) {
                mPaymentRequestUI.updateSection(
                        PaymentRequestUI.DataType.SHIPPING_OPTIONS, mUiShippingOptions);
            }
        }
        return isFirstUpdate;
    }

    /** Implements {@link PaymentRequestUI.Client.shouldShowShippingSection}. */
    public boolean shouldShowShippingSection() {
        if (!PaymentOptionsUtils.requestShipping(mParams.getPaymentOptions())) return false;

        if (mPaymentMethodsSection == null) return true;

        PaymentApp selectedApp = (PaymentApp) mPaymentMethodsSection.getSelectedItem();
        return selectedApp == null || !selectedApp.handlesShippingAddress();
    }

    /** Implements {@link PaymentRequestUI.Client.shouldShowContactSection}. */
    public boolean shouldShowContactSection() {
        PaymentApp selectedApp = (mPaymentMethodsSection == null)
                ? null
                : (PaymentApp) mPaymentMethodsSection.getSelectedItem();
        org.chromium.payments.mojom.PaymentOptions options = mParams.getPaymentOptions();
        if (PaymentOptionsUtils.requestPayerName(options)
                && (selectedApp == null || !selectedApp.handlesPayerName())) {
            return true;
        }
        if (PaymentOptionsUtils.requestPayerPhone(options)
                && (selectedApp == null || !selectedApp.handlesPayerPhone())) {
            return true;
        }
        if (PaymentOptionsUtils.requestPayerEmail(options)
                && (selectedApp == null || !selectedApp.handlesPayerEmail())) {
            return true;
        }

        return false;
    }

    // Implement PaymentHandlerUiObserver:
    @Override
    public void onPaymentHandlerUiClosed() {
        mPaymentUisShowStateReconciler.onBottomSheetClosed();
        mPaymentHandlerUi = null;
    }

    // Implement PaymentHandlerUiObserver:
    @Override
    public void onPaymentHandlerUiShown() {
        assert mPaymentHandlerUi != null;
        mPaymentUisShowStateReconciler.onBottomSheetShown();
    }

    /** Close the PaymentHandler UI if not already. */
    public void ensureHideAndResetPaymentHandlerUi() {
        if (mPaymentHandlerUi == null) return;
        mPaymentHandlerUi.hide();
        mPaymentHandlerUi = null;
    }

    /**
     * Create and show the (BottomSheet) PaymentHandler UI.
     * @param webContents The WebContents of the merchant page.
     * @param url The URL of the payment app.
     * @param paymentHandlerWebContentsObserver An observer of the WebContents of the Payment
     *         Handler UI.
     * @param isOffTheRecord Whether the merchant page is currently in an OffTheRecord tab.
     * @return Whether the PaymentHandler UI is shown successfully.
     */
    public boolean showPaymentHandlerUI(WebContents webContents, GURL url,
            PaymentHandlerWebContentsObserver paymentHandlerWebContentsObserver,
            boolean isOffTheRecord) {
        if (mPaymentHandlerUi != null) return false;
        ChromeActivity chromeActivity = ChromeActivity.fromWebContents(webContents);
        if (chromeActivity == null) return false;

        mPaymentHandlerUi = new PaymentHandlerCoordinator();
        return mPaymentHandlerUi.show(chromeActivity, url, isOffTheRecord,
                paymentHandlerWebContentsObserver, /*uiObserver=*/this);
    }

    @VisibleForTesting(otherwise = VisibleForTesting.NONE)
    public WebContents getPaymentHandlerWebContentsForTest() {
        if (mPaymentHandlerUi == null) return null;
        return mPaymentHandlerUi.getWebContentsForTest();
    }

    @VisibleForTesting(otherwise = VisibleForTesting.NONE)
    public boolean clickPaymentHandlerSecurityIconForTest() {
        if (mPaymentHandlerUi == null) return false;
        mPaymentHandlerUi.clickSecurityIconForTest();
        return true;
    }

    /** Provide PaymentInformation to the PaymentRequest UI. */
    public void providePaymentInformationToPaymentRequestUI() {
        // Do not display service worker payment apps summary in single line so as to display its
        // origin completely.
        mPaymentMethodsSection.setDisplaySelectedItemSummaryInSingleLineInNormalMode(
                getSelectedPaymentAppType() != PaymentAppType.SERVICE_WORKER_APP);
        mPaymentInformationCallback.onResult(
                new PaymentInformation(mUiShoppingCart, mShippingAddressesSection,
                        mUiShippingOptions, mContactSection, mPaymentMethodsSection));
        mPaymentInformationCallback = null;
    }

    /**
     * Edit the contact information on the PaymentRequest UI.
     * @param toEdit The information to edit, allowed to be null.
     **/
    public void editContactOnPaymentRequestUI(@Nullable final AutofillContact toEdit) {
        mContactEditor.edit(toEdit, new Callback<AutofillContact>() {
            @Override
            public void onResult(AutofillContact editedContact) {
                if (mPaymentRequestUI == null) return;

                if (editedContact != null) {
                    mContactEditor.setPayerErrors(null);

                    // A partial or complete contact came back from the editor (could have been from
                    // adding/editing or cancelling out of the edit flow).
                    if (!editedContact.isComplete()) {
                        // If the contact is not complete according to the requirements of the flow,
                        // unselect it (editor can return incomplete information when cancelled).
                        mContactSection.setSelectedItemIndex(SectionInformation.NO_SELECTION);
                    } else if (toEdit == null) {
                        // Contact is complete and we were in the "Add flow": add an item to the
                        // list.
                        mContactSection.addAndSelectItem(editedContact);
                    } else {
                        mDelegate.dispatchPayerDetailChangeEventIfNeeded(
                                editedContact.toPayerDetail());
                    }
                    // If contact is complete and (toEdit != null), no action needed: the contact
                    // was already selected in the UI.
                }
                // If |editedContact| is null, the user has cancelled out of the "Add flow". No
                // action to take (if a contact was selected in the UI, it will stay selected).

                mPaymentRequestUI.updateSection(
                        PaymentRequestUI.DataType.CONTACT_DETAILS, mContactSection);

                if (!mRetryQueue.isEmpty()) mHandler.post(mRetryQueue.remove());
            }
        });
    }

    /**
     * Edit the address on the PaymentRequest UI.
     * @param toEdit The address to be updated with, allowed to be null.
     */
    public void editAddress(@Nullable final AutofillAddress toEdit) {
        mAddressEditor.edit(toEdit, new Callback<AutofillAddress>() {
            @Override
            public void onResult(AutofillAddress editedAddress) {
                if (mPaymentRequestUI == null) return;

                if (editedAddress != null) {
                    mAddressEditor.setAddressErrors(null);

                    // Sets or updates the shipping address label.
                    editedAddress.setShippingAddressLabelWithCountry();

                    mCardEditor.updateBillingAddressIfComplete(editedAddress);

                    // A partial or complete address came back from the editor (could have been from
                    // adding/editing or cancelling out of the edit flow).
                    if (!editedAddress.isComplete()) {
                        // If the address is not complete, unselect it (editor can return incomplete
                        // information when cancelled).
                        mShippingAddressesSection.setSelectedItemIndex(
                                SectionInformation.NO_SELECTION);
                        providePaymentInformationToPaymentRequestUI();
                        mDelegate.recordShowEventAndTransactionAmount();
                    } else {
                        if (toEdit == null) {
                            // Address is complete and user was in the "Add flow": add an item to
                            // the list.
                            mShippingAddressesSection.addAndSelectItem(editedAddress);
                        }

                        if (mContactSection != null) {
                            // Update |mContactSection| with the new/edited
                            // address, which will update an existing item or add a new one to the
                            // end of the list.
                            mContactSection.addOrUpdateWithAutofillAddress(editedAddress);
                            mPaymentRequestUI.updateSection(
                                    PaymentRequestUI.DataType.CONTACT_DETAILS, mContactSection);
                        }

                        mDelegate.startShippingAddressChangeNormalization(editedAddress);
                    }
                } else {
                    providePaymentInformationToPaymentRequestUI();
                    mDelegate.recordShowEventAndTransactionAmount();
                }

                if (!mRetryQueue.isEmpty()) mHandler.post(mRetryQueue.remove());
            }
        });
    }

    /** Create a shipping section for PaymentRequest UI. */
    public void createShippingSectionForPaymentRequestUI(Context context) {
        List<AutofillAddress> addresses = new ArrayList<>();

        for (int i = 0; i < mAutofillProfiles.size(); i++) {
            AutofillProfile profile = mAutofillProfiles.get(i);
            mAddressEditor.addPhoneNumberIfValid(profile.getPhoneNumber());

            // Only suggest addresses that have a street address.
            if (!TextUtils.isEmpty(profile.getStreetAddress())) {
                addresses.add(new AutofillAddress(context, profile));
            }
        }

        // Suggest complete addresses first.
        Collections.sort(addresses, COMPLETENESS_COMPARATOR);

        // Limit the number of suggestions.
        addresses = addresses.subList(0, Math.min(addresses.size(), SUGGESTIONS_LIMIT));

        // Load the validation rules for each unique region code.
        Set<String> uniqueCountryCodes = new HashSet<>();
        for (int i = 0; i < addresses.size(); ++i) {
            String countryCode = AutofillAddress.getCountryCode(addresses.get(i).getProfile());
            if (!uniqueCountryCodes.contains(countryCode)) {
                uniqueCountryCodes.add(countryCode);
                PersonalDataManager.getInstance().loadRulesForAddressNormalization(countryCode);
            }
        }

        // Automatically select the first address if one is complete and if the merchant does
        // not require a shipping address to calculate shipping costs.
        boolean hasCompleteShippingAddress = !addresses.isEmpty() && addresses.get(0).isComplete();
        int firstCompleteAddressIndex = SectionInformation.NO_SELECTION;
        if (mUiShippingOptions.getSelectedItem() != null && hasCompleteShippingAddress) {
            firstCompleteAddressIndex = 0;

            // The initial label for the selected shipping address should not include the
            // country.
            addresses.get(firstCompleteAddressIndex).setShippingAddressLabelWithoutCountry();
        }

        // Log the number of suggested shipping addresses and whether at least one of them is
        // complete.
        mJourneyLogger.setNumberOfSuggestionsShown(
                Section.SHIPPING_ADDRESS, addresses.size(), hasCompleteShippingAddress);

        int missingFields = 0;
        if (addresses.isEmpty()) {
            // All fields are missing.
            missingFields = AutofillAddress.CompletionStatus.INVALID_RECIPIENT
                    | AutofillAddress.CompletionStatus.INVALID_PHONE_NUMBER
                    | AutofillAddress.CompletionStatus.INVALID_ADDRESS;
        } else {
            missingFields = addresses.get(0).getMissingFieldsOfShippingProfile();
        }
        if (missingFields != 0) {
            RecordHistogram.recordSparseHistogram(
                    "PaymentRequest.MissingShippingFields", missingFields);
        }

        mShippingAddressesSection = new SectionInformation(
                PaymentRequestUI.DataType.SHIPPING_ADDRESSES, firstCompleteAddressIndex, addresses);
    }

    /**
     * Rank the payment apps for PaymentRequest UI.
     * @param paymentApps A list of payment apps to be ranked in place.
     */
    public void rankPaymentAppsForPaymentRequestUI(List<PaymentApp> paymentApps) {
        Collections.sort(paymentApps, mPaymentAppComparator);
    }

    /**
     * Edit the credit cards on the PaymentRequest UI.
     * @param toEdit The AutofillPaymentInstrument whose credit card is to replace those on the UI,
     *         allowed to be null.
     */
    public void editCard(@Nullable final AutofillPaymentInstrument toEdit) {
        if (toEdit != null) {
            // Log the edit of a credit card.
            mJourneyLogger.incrementSelectionEdits(Section.PAYMENT_METHOD);
        }
        mCardEditor.edit(toEdit, new Callback<AutofillPaymentInstrument>() {
            @Override
            public void onResult(AutofillPaymentInstrument editedCard) {
                if (mPaymentRequestUI == null) return;

                if (editedCard != null) {
                    // A partial or complete card came back from the editor (could have been from
                    // adding/editing or cancelling out of the edit flow).
                    if (!editedCard.isComplete()) {
                        // If the card is not complete, unselect it (editor can return incomplete
                        // information when cancelled).
                        mPaymentMethodsSection.setSelectedItemIndex(
                                SectionInformation.NO_SELECTION);
                    } else if (toEdit == null) {
                        // Card is complete and we were in the "Add flow": add an item to the list.
                        mPaymentMethodsSection.addAndSelectItem(editedCard);
                    }
                    // If card is complete and (toEdit != null), no action needed: the card was
                    // already selected in the UI.
                }
                // If |editedCard| is null, the user has cancelled out of the "Add flow". No action
                // to take (if another card was selected prior to the add flow, it will stay
                // selected).

                updateAppModifiedTotals();
                mPaymentRequestUI.updateSection(
                        PaymentRequestUI.DataType.PAYMENT_METHODS, mPaymentMethodsSection);
            }
        });
    }

    /** See {@link PaymentRequestUI.getSelectionInformation}. */
    public void getSectionInformation(@PaymentRequestUI.DataType final int optionType,
            final Callback<SectionInformation> callback) {
        SectionInformation result = null;
        switch (optionType) {
            case PaymentRequestUI.DataType.SHIPPING_ADDRESSES:
                result = mShippingAddressesSection;
                break;
            case PaymentRequestUI.DataType.SHIPPING_OPTIONS:
                result = mUiShippingOptions;
                break;
            case PaymentRequestUI.DataType.CONTACT_DETAILS:
                result = mContactSection;
                break;
            case PaymentRequestUI.DataType.PAYMENT_METHODS:
                result = mPaymentMethodsSection;
                break;
            default:
                assert false;
        }
        mHandler.post(callback.bind(result));
    }

    // Implement PaymentRequestSection.FocusChangedObserver:
    @Override
    public void onFocusChanged(@PaymentRequestUI.DataType int dataType, boolean willFocus) {
        assert dataType == PaymentRequestUI.DataType.SHIPPING_ADDRESSES;

        if (mShippingAddressesSection.getSelectedItem() == null) return;

        AutofillAddress selectedAddress =
                (AutofillAddress) mShippingAddressesSection.getSelectedItem();

        // The label should only include the country if the view is focused.
        if (willFocus) {
            selectedAddress.setShippingAddressLabelWithCountry();
        } else {
            selectedAddress.setShippingAddressLabelWithoutCountry();
        }

        mPaymentRequestUI.updateSection(
                PaymentRequestUI.DataType.SHIPPING_ADDRESSES, mShippingAddressesSection);
    }

    /** See {@link PaymentRequestUI.Client.onSectionAddOption}. */
    public int onSectionAddOption(
            @PaymentRequestUI.DataType int optionType, Callback<PaymentInformation> callback) {
        if (optionType == PaymentRequestUI.DataType.SHIPPING_ADDRESSES) {
            editAddress(null);
            mPaymentInformationCallback = callback;
            // Log the add of shipping address.
            mJourneyLogger.incrementSelectionAdds(Section.SHIPPING_ADDRESS);
            return PaymentRequestUI.SelectionResult.ASYNCHRONOUS_VALIDATION;
        } else if (optionType == PaymentRequestUI.DataType.CONTACT_DETAILS) {
            editContactOnPaymentRequestUI(null);
            // Log the add of contact info.
            mJourneyLogger.incrementSelectionAdds(Section.CONTACT_INFO);
            return PaymentRequestUI.SelectionResult.EDITOR_LAUNCH;
        } else if (optionType == PaymentRequestUI.DataType.PAYMENT_METHODS) {
            editCard(null);
            // Log the add of credit card.
            mJourneyLogger.incrementSelectionAdds(Section.PAYMENT_METHOD);
            return PaymentRequestUI.SelectionResult.EDITOR_LAUNCH;
        }

        return PaymentRequestUI.SelectionResult.NONE;
    }

    @PaymentRequestUI.SelectionResult
    public int onSectionEditOption(@PaymentRequestUI.DataType int optionType, EditableOption option,
            Callback<PaymentInformation> callback) {
        if (optionType == PaymentRequestUI.DataType.SHIPPING_ADDRESSES) {
            // Log the edit of a shipping address.
            mJourneyLogger.incrementSelectionEdits(Section.SHIPPING_ADDRESS);
            editAddress((AutofillAddress) option);
            mPaymentInformationCallback = callback;

            return PaymentRequestUI.SelectionResult.ASYNCHRONOUS_VALIDATION;
        }

        if (optionType == PaymentRequestUI.DataType.CONTACT_DETAILS) {
            mJourneyLogger.incrementSelectionEdits(Section.CONTACT_INFO);
            editContactOnPaymentRequestUI((AutofillContact) option);
            return PaymentRequestUI.SelectionResult.EDITOR_LAUNCH;
        }

        if (optionType == PaymentRequestUI.DataType.PAYMENT_METHODS) {
            editCard((AutofillPaymentInstrument) option);
            return PaymentRequestUI.SelectionResult.EDITOR_LAUNCH;
        }

        assert false;
        return PaymentRequestUI.SelectionResult.NONE;
    }

    /** Set a change observer for the shipping address section on the PaymentRequest UI. */
    public void setShippingAddressSectionFocusChangedObserverForPaymentRequestUI() {
        mPaymentRequestUI.setShippingAddressSectionFocusChangedObserver(this);
    }

    /**
     * @return true when there is exactly one available payment app which can provide all requested
     * information including shipping address and payer's contact information whenever needed.
     */
    public boolean onlySingleAppCanProvideAllRequiredInformation() {
        assert mPaymentMethodsSection != null;

        if (!PaymentOptionsUtils.requestAnyInformation(mParams.getPaymentOptions())) {
            return mPaymentMethodsSection.getSize() == 1
                    && !((PaymentApp) mPaymentMethodsSection.getItem(0)).isAutofillInstrument();
        }

        boolean anAppCanProvideAllInfo = false;
        int sectionSize = mPaymentMethodsSection.getSize();
        for (int i = 0; i < sectionSize; i++) {
            PaymentApp app = (PaymentApp) mPaymentMethodsSection.getItem(i);
            if ((!PaymentOptionsUtils.requestShipping(mParams.getPaymentOptions())
                        || app.handlesShippingAddress())
                    && (!PaymentOptionsUtils.requestPayerName(mParams.getPaymentOptions())
                            || app.handlesPayerName())
                    && (!PaymentOptionsUtils.requestPayerPhone(mParams.getPaymentOptions())
                            || app.handlesPayerPhone())
                    && (!PaymentOptionsUtils.requestPayerEmail(mParams.getPaymentOptions())
                            || app.handlesPayerEmail())) {
                // There is more than one available app that can provide all merchant requested
                // information information.
                if (anAppCanProvideAllInfo) return false;

                anAppCanProvideAllInfo = true;
            }
        }
        return anAppCanProvideAllInfo;
    }
}
