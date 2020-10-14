// Copyright 2020 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

/**
 * @fileoverview
 * 'settings-nearby-share-subpage' is the settings subpage for managing the
 * Nearby Share feature.
 */
Polymer({
  is: 'settings-nearby-share-subpage',

  behaviors: [
    I18nBehavior,
    PrefsBehavior,
    settings.RouteObserverBehavior,
  ],

  properties: {
    /** Preferences state. */
    prefs: {
      type: Object,
      notify: true,
    },

    /** @private {boolean} */
    showDeviceNameDialog_: {
      type: Boolean,
      value: false,
    },

    /** @private {boolean} */
    showDataUsageDialog_: {
      type: Boolean,
      value: false,
    },
  },

  /**
   * @param {!Event} event
   * @private
   */
  onEnableTap_(event) {
    this.setPrefValue(
        'nearby_sharing.enabled',
        !this.getPref('nearby_sharing.enabled').value);
    event.stopPropagation();
  },

  /** @private */
  onDeviceNameTap_() {
    if (this.showDeviceNameDialog_) {
      return;
    }
    this.showDeviceNameDialog_ = true;
  },

  /** @private */
  onDataUsageTap_() {
    this.showDataUsageDialog_ = true;
  },

  /**
   * @param {!Event} event
   * @private
   */
  onDeviceNameDialogClose_(event) {
    this.showDeviceNameDialog_ = false;
  },

  /**
   * @param {!Event} event
   * @private
   */
  onDataUsageDialogClose_(event) {
    this.showDataUsageDialog_ = false;
  },

  /**
   * @param {boolean} state boolean state that determines which string to show
   * @param {string} onstr string to show when state is true
   * @param {string} offstr string to show when state is false
   * @return {string} localized string
   * @private
   */
  getOnOffString_(state, onstr, offstr) {
    return state ? onstr : offstr;
  },

  /**
   * @param {string} name name of device
   * @return {string} localized string
   * @private
   */
  getEditNameButtonAriaDescription_(name) {
    return this.i18n('nearbyShareDeviceNameAriaDescription', name);
  },

  /**
   * @param {string} dataUsageValue enum value of data usage setting.
   * @return {string} localized string
   * @private
   */
  getDataUsageLabel_(dataUsageValue) {
    if (dataUsageStringToEnum(dataUsageValue) === NearbyShareDataUsage.ONLINE) {
      return this.i18n('nearbyShareDataUsageDataLabel');
    } else if (
        dataUsageStringToEnum(dataUsageValue) ===
        NearbyShareDataUsage.OFFLINE) {
      return this.i18n('nearbyShareDataUsageOfflineLabel');
    } else {
      return this.i18n('nearbyShareDataUsageWifiOnlyLabel');
    }
  },

  /**
   * @param {string} dataUsageValue enum value of data usage setting.
   * @return {string} localized string
   * @private
   */
  getDataUsageSubLabel_(dataUsageValue) {
    if (dataUsageStringToEnum(dataUsageValue) === NearbyShareDataUsage.ONLINE) {
      return this.i18n('nearbyShareDataUsageDataDescription');
    } else if (
        dataUsageStringToEnum(dataUsageValue) ===
        NearbyShareDataUsage.OFFLINE) {
      return this.i18n('nearbyShareDataUsageOfflineDescription');
    } else {
      return this.i18n('nearbyShareDataUsageWifiOnlyDescription');
    }
  },

  /**
   * @param {string} dataUsageValue enum value of data usage setting.
   * @return {string} localized string
   * @private
   */
  getEditDataUsageButtonAriaDescription_(dataUsageValue) {
    if (dataUsageStringToEnum(dataUsageValue) === NearbyShareDataUsage.ONLINE) {
      return this.i18n('nearbyShareDataUsageDataEditButtonDescription');
    } else if (
        dataUsageStringToEnum(dataUsageValue) ===
        NearbyShareDataUsage.OFFLINE) {
      return this.i18n('nearbyShareDataUsageOfflineEditButtonDescription');
    } else {
      return this.i18n('nearbyShareDataUsageWifiOnlyEditButtonDescription');
    }
  },

  /**
   * @param {!settings.Route} route
   */
  currentRouteChanged(route) {
    const router = settings.Router.getInstance();
    if (router.getCurrentRoute().path.endsWith('nearbyshare')) {
      const queryParams = router.getQueryParameters();
      if (queryParams.has('deviceName')) {
        this.showDeviceNameDialog_ = true;
      }
    }
  },
});
