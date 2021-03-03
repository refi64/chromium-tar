// Copyright 2020 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.chrome.browser.tasks.tab_management;

import androidx.annotation.VisibleForTesting;

import org.chromium.chrome.browser.preferences.ChromePreferenceKeys;
import org.chromium.chrome.browser.preferences.SharedPreferencesManager;
import org.chromium.chrome.browser.profiles.Profile;
import org.chromium.chrome.browser.signin.services.IdentityServicesProvider;
import org.chromium.chrome.browser.sync.ProfileSyncService;
import org.chromium.components.sync.ModelType;

/**
 * A class to handle whether price tracking-related features are turned on by users,
 * including tracking prices on tabs and price drop alerts.
 * Whether the feature is available is controlled by {@link
 * TabUiFeatureUtilities#ENABLE_PRICE_TRACKING}.
 */
public class PriceTrackingUtilities {
    @VisibleForTesting
    public static final String TRACK_PRICES_ON_TABS =
            ChromePreferenceKeys.PRICE_TRACKING_TRACK_PRICES_ON_TABS;
    private static final String PRICE_DROP_ALERTS =
            ChromePreferenceKeys.PRICE_TRACKING_PRICE_DROP_ALERTS;
    @VisibleForTesting
    public static final String PRICE_WELCOME_MESSAGE_CARD =
            ChromePreferenceKeys.PRICE_TRACKING_PRICE_WELCOME_MESSAGE_CARD;
    @VisibleForTesting
    public static final String PRICE_WELCOME_MESSAGE_CARD_SHOW_COUNT =
            ChromePreferenceKeys.PRICE_TRACKING_PRICE_WELCOME_MESSAGE_CARD_SHOW_COUNT;

    @VisibleForTesting
    public static final SharedPreferencesManager SHARED_PREFERENCES_MANAGER =
            SharedPreferencesManager.getInstance();

    private static Boolean sIsSignedInAndSyncEnabledForTesting;

    /**
     * @return Whether the price tracking feature is eligible to work. Now it is used to determine
     *         whether the menu item "track prices" is visible and whether the tab has {@link
     *         TabProperties#SHOPPING_PERSISTED_TAB_DATA_FETCHER}.
     */
    public static boolean isPriceTrackingEligible() {
        if (sIsSignedInAndSyncEnabledForTesting != null) {
            return TabUiFeatureUtilities.isPriceTrackingEnabled()
                    && sIsSignedInAndSyncEnabledForTesting;
        }
        return TabUiFeatureUtilities.isPriceTrackingEnabled() && isSignedIn()
                && isOpenTabsSyncEnabled();
    }

    /**
     * Update SharedPreferences when users turn on/off the feature tracking prices on tabs.
     */
    public static void flipTrackPricesOnTabs() {
        final boolean enableTrackPricesOnTabs = SHARED_PREFERENCES_MANAGER.readBoolean(
                TRACK_PRICES_ON_TABS, TabUiFeatureUtilities.isPriceTrackingEnabled());
        SHARED_PREFERENCES_MANAGER.writeBoolean(TRACK_PRICES_ON_TABS, !enableTrackPricesOnTabs);
    }

    /**
     * @return Whether the track prices on tabs is turned on by users.
     */
    public static boolean isTrackPricesOnTabsEnabled() {
        return isPriceTrackingEligible()
                && SHARED_PREFERENCES_MANAGER.readBoolean(
                        TRACK_PRICES_ON_TABS, TabUiFeatureUtilities.isPriceTrackingEnabled());
    }

    /**
     * Update SharedPreferences when users turn on/off the feature getting price drop alerts.
     */
    public static void flipPriceDropAlerts() {
        final boolean enablePriceDropAlerts =
                SHARED_PREFERENCES_MANAGER.readBoolean(PRICE_DROP_ALERTS, false);
        SHARED_PREFERENCES_MANAGER.writeBoolean(PRICE_DROP_ALERTS, !enablePriceDropAlerts);
    }

    /**
     * @return Whether the feature getting price drop alerts is turned on by users.
     */
    public static boolean isPriceDropAlertsEnabled() {
        return isPriceTrackingEligible()
                && SHARED_PREFERENCES_MANAGER.readBoolean(PRICE_DROP_ALERTS, false);
    }

    /**
     * Forbid showing the PriceWelcomeMessageCard any more.
     */
    public static void disablePriceWelcomeMessageCard() {
        SHARED_PREFERENCES_MANAGER.writeBoolean(PRICE_WELCOME_MESSAGE_CARD, false);
    }

    /**
     * @return Whether the PriceWelcomeMessageCard is disabled by users.
     */
    public static boolean isPriceWelcomeMessageCardDisabled() {
        return !SHARED_PREFERENCES_MANAGER.readBoolean(
                PRICE_WELCOME_MESSAGE_CARD, TabUiFeatureUtilities.isPriceTrackingEnabled());
    }

    /**
     * Increase the show count of PriceWelcomeMessageCard every time it shows in the tab switcher.
     */
    public static void increasePriceWelcomeMessageCardShowCount() {
        SHARED_PREFERENCES_MANAGER.writeInt(
                PRICE_WELCOME_MESSAGE_CARD_SHOW_COUNT, getPriceWelcomeMessageCardShowCount() + 1);
    }

    /**
     * @return The show count of PriceWelcomeMessageCard.
     */
    public static int getPriceWelcomeMessageCardShowCount() {
        return SHARED_PREFERENCES_MANAGER.readInt(PRICE_WELCOME_MESSAGE_CARD_SHOW_COUNT, 0);
    }

    private static boolean isSignedIn() {
        return IdentityServicesProvider.get()
                .getIdentityManager(Profile.getLastUsedRegularProfile())
                .hasPrimaryAccount();
    }

    private static boolean isOpenTabsSyncEnabled() {
        ProfileSyncService syncService = ProfileSyncService.get();
        return syncService != null && syncService.isSyncRequested()
                && syncService.getActiveDataTypes().contains(ModelType.SESSIONS);
    }

    @VisibleForTesting
    public static void setIsSignedInAndSyncEnabledForTesting(Boolean isSignedInAndSyncEnabled) {
        sIsSignedInAndSyncEnabledForTesting = isSignedInAndSyncEnabled;
    }
}
