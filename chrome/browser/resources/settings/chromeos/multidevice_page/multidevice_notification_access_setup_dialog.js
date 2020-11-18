// Copyright 2020 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

/**
 * @fileoverview
 * This element provides the Phone Hub notification access setup flow that, when
 * successfully completed, enables the feature that allows a user's phone
 * notifications to be mirrored on their Chromebook.
 */

/**
 * Numerical values should not be changed because they must stay in sync with
 * notification_access_setup_operation.h, with the exception of NOT_STARTED.
 * @enum{number}
 */
/* #export */ const NotificationAccessSetupOperationStatus = {
  NOT_STARTED: 0,
  CONNECTING: 1,
  TIMED_OUT_CONNECTING: 2,
  CONNECTION_DISCONNECTED: 3,
  SENT_MESSAGE_TO_PHONE_AND_WAITING_FOR_RESPONSE: 4,
  COMPLETED_SUCCESSFULLY: 5,
};

Polymer({
  is: 'settings-multidevice-notification-access-setup-dialog',

  behaviors: [
    I18nBehavior,
    WebUIListenerBehavior,
  ],

  properties: {
    /** @private {NotificationAccessSetupOperationStatus} */
    setupState_: {
      type: Number,
      value: NotificationAccessSetupOperationStatus.NOT_STARTED,
    },

    /** @private */
    title_: {
      type: String,
      computed: 'getTitle_(setupState_)',
    },

    /** @private */
    description_: {
      type: String,
      computed: 'getDescription_(setupState_)',
    }
  },

  /** @private {?settings.MultiDeviceBrowserProxy} */
  browserProxy_: null,

  /** @override */
  ready() {
    this.browserProxy_ = settings.MultiDeviceBrowserProxyImpl.getInstance();
  },

  /** @override */
  attached() {
    this.addWebUIListener(
        'settings.onNotificationAccessSetupStatusChanged',
        this.onSetupStateChanged_.bind(this));
    this.$.dialog.showModal();
  },

  /**
   * @param {!NotificationAccessSetupOperationStatus} setupState
   * @private
   */
  onSetupStateChanged_(setupState) {
    this.setupState_ = setupState;
  },

  /** @private */
  showCancelButton_() {
    return this.setupState_ ===
        NotificationAccessSetupOperationStatus.NOT_STARTED ||
        this.setupState_ ===
        NotificationAccessSetupOperationStatus.CONNECTING ||
        this.setupState_ ===
        NotificationAccessSetupOperationStatus
            .SENT_MESSAGE_TO_PHONE_AND_WAITING_FOR_RESPONSE;
  },

  /** @private */
  showOkButton_() {
    return this.setupState_ ===
        NotificationAccessSetupOperationStatus.COMPLETED_SUCCESSFULLY;
  },

  /** @private */
  onConfirmButtonClicked_() {
    this.browserProxy_.attemptNotificationSetup();
  },

  /** @private */
  onCancelClicked_() {
    this.browserProxy_.cancelNotificationSetup();
    this.$.dialog.close();
  },

  /** @private */
  onOkayButtonClicked_() {
    this.$.dialog.close();
  },

  /**
   * @return {string} The title of the dialog.
   * @private
   */
  getTitle_() {
    const Status = NotificationAccessSetupOperationStatus;
    switch (this.setupState_) {
      case Status.NOT_STARTED:
        return this.i18n('multideviceNotificationAccessSetupAckTitle');
      case Status.CONNECTING:
      case Status.SENT_MESSAGE_TO_PHONE_AND_WAITING_FOR_RESPONSE:
        return this.i18n('multideviceNotificationAccessSetupConnectingTitle');
      case Status.COMPLETED_SUCCESSFULLY:
        return this.i18n('multideviceNotificationAccessSetupCompletedTitle');
      case Status.TIMED_OUT_CONNECTING:
      case Status.CONNECTION_DISCONNECTED:
      default:
        return '';
    }
  },

  /**
   * @return {string} The body text of the dialog.
   * @private
   */
  getDescription_() {
    const Status = NotificationAccessSetupOperationStatus;
    switch (this.setupState_) {
      case Status.NOT_STARTED:
      case Status.CONNECTING:
      case Status.SENT_MESSAGE_TO_PHONE_AND_WAITING_FOR_RESPONSE:
        return this.i18n('multideviceNotificationAccessSetupInstructions');
      case Status.COMPLETED_SUCCESSFULLY:
        return this.i18n('multideviceNotificationAccessSetupCompletedSummary');
      case Status.TIMED_OUT_CONNECTING:
      case Status.CONNECTION_DISCONNECTED:
      default:
        return '';
    }
  },
});
