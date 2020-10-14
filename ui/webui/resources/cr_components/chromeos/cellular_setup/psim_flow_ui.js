// Copyright 2020 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

cr.define('cellularSetup', function() {
  /** @enum{string} */
  const PSimPageName = {
    SIM_DETECT: 'sim-detect-page',
    PROVISIONING: 'provisioning-page',
    FINAL: 'final-page',
  };

  /** @enum{string} */
  const PSimUIState = {
    IDLE: 'idle',
    STARTING_ACTIVATION: 'starting-activation',
    WAITING_FOR_ACTIVATION_TO_START: 'waiting-for-activation-to-start',
    TIMEOUT_START_ACTIVATION: 'timeout-start-activation',
    WAITING_FOR_PORTAL_TO_LOAD: 'waiting-for-portal-to-load',
    TIMEOUT_PORTAL_LOAD: 'timeout-portal-load',
    WAITING_FOR_USER_PAYMENT: 'waiting-for-user-payment',
    WAITING_FOR_ACTIVATION_TO_FINISH: 'waiting-for-activation-to-finish',
    TIMEOUT_FINISH_ACTIVATION: 'timeout-finish-activation',
    ACTIVATION_SUCCESS: 'activation-success',
    ALREADY_ACTIVATED: 'already-activated',
    ACTIVATION_FAILURE: 'activation-failure',
  };

  /**
   * @param {!cellularSetup.PSimUIState} state
   * @return {?number} The time delta, in ms, for the timeout corresponding to
   *     |state|. If no timeout is applicable for this state, null is returned.
   */
  function getTimeoutMsForPSimUIState(state) {
    // In some cases, starting activation may require power-cycling the device's
    // modem, a process that can take several seconds.
    if (state === PSimUIState.STARTING_ACTIVATION) {
      return 10000;  // 10 seconds.
    }

    // The portal is a website served by the mobile carrier.
    if (state === PSimUIState.WAITING_FOR_PORTAL_TO_LOAD) {
      return 10000;  // 10 seconds.
    }

    // Finishing activation only requires sending a D-Bus message to Shill.
    if (state === PSimUIState.WAITING_FOR_ACTIVATION_TO_FINISH) {
      return 1000;  // 1 second.
    }

    // No other states require timeouts.
    return null;
  }

  return {
    PSimPageName: PSimPageName,
    PSimUIState: PSimUIState,
    getTimeoutMsForPSimUIState: getTimeoutMsForPSimUIState
  };
});

/**
 * Root element for the pSIM cellular setup flow. This element interacts with
 * the CellularSetup service to carry out the psim activation flow. It contains
 * navigation buttons and sub-pages corresponding to each step of the flow.
 */
Polymer({
  is: 'psim-flow-ui',

  behaviors: [I18nBehavior],

  properties: {
    /** @private {!cellularSetup.PSimUIState} */
    state_: {
      type: String,
      value: cellularSetup.PSimUIState.IDLE,
    },

    /**
     * Element name of the current selected sub-page.
     * @private {!cellularSetup.PSimPageName}
     */
    selectedPSimPageName_: {
      type: String,
      value: cellularSetup.PSimPageName.SIM_DETECT,
      notify: true,
    },

    /**
     * DOM Element for the current selected sub-page.
     * @private {!SimDetectPageElement|!ProvisioningPageElement|
     *           !FinalPageElement}
     */
    selectedPage_: Object,

    /**
     * Whether error state should be shown for the current page.
     * @private {boolean}
     */
    showError_: {type: Boolean, value: false},

    /**
     * Cellular metadata received via the onActivationStarted() callback. If
     * that callback has not occurred, this field is null.
     * @private {?chromeos.cellularSetup.mojom.CellularMetadata}
     */
    cellularMetadata_: {
      type: Object,
      value: null,
    },

    /**
     * Whether try again should be shown in the button bar.
     * @private {boolean}
     */
    showTryAgainButton_: {type: Boolean, value: false},

    /**
     * Whether finish button should be shown in the button bar.
     * @private {boolean}
     */
    showFinishButton_: {type: Boolean, value: false},

    /**
     * Whether cancel button should be shown in the button bar.
     * @private {boolean}
     */
    showCancelButton_: {type: Boolean, value: false}
  },

  observers: [
    'updateShowError_(state_)',
    'updateSelectedPage_(state_)',
    'handlePSimUIStateChange_(state_)',
  ],

  /**
   * Provides an interface to the CellularSetup Mojo service.
   * @private {?cellular_setup.MojoInterfaceProvider}
   */
  mojoInterfaceProvider_: null,

  /**
   * Delegate responsible for routing activation started/finished events.
   * @private {?chromeos.cellularSetup.mojom.ActivationDelegateReceiver}
   */
  activationDelegateReceiver_: null,

  /**
   * The timeout ID corresponding to a timeout for the current state. If no
   * timeout is active, this value is null.
   * @private {?number}
   */
  currentTimeoutId_: null,

  /**
   * Handler used to communicate state updates back to the CellularSetup
   * service.
   * @private {?chromeos.cellularSetup.mojom.CarrierPortalHandlerRemote}
   */
  carrierPortalHandler_: null,

  /** @override */
  created() {
    this.mojoInterfaceProvider_ =
        cellular_setup.MojoInterfaceProviderImpl.getInstance();
  },

  /** @override */
  ready() {
    this.state_ = cellularSetup.PSimUIState.STARTING_ACTIVATION;
  },

  /**
   * Overrides chromeos.cellularSetup.mojom.ActivationDelegateInterface.
   * @param {!chromeos.cellularSetup.mojom.CellularMetadata} metadata
   * @private
   */
  onActivationStarted(metadata) {
    this.clearTimer_();
    this.cellularMetadata_ = metadata;
    this.state_ = cellularSetup.PSimUIState.WAITING_FOR_PORTAL_TO_LOAD;
  },

  /**
   * Overrides chromeos.cellularSetup.mojom.ActivationDelegateInterface.
   * @param {!chromeos.cellularSetup.mojom.ActivationResult} result
   * @private
   */
  onActivationFinished(result) {
    this.closeActivationConnection_();

    const ActivationResult = chromeos.cellularSetup.mojom.ActivationResult;
    switch (result) {
      case ActivationResult.kSuccessfullyStartedActivation:
        this.state_ = cellularSetup.PSimUIState.ALREADY_ACTIVATED;
        break;
      case ActivationResult.kAlreadyActivated:
        this.state_ = cellularSetup.PSimUIState.ACTIVATION_SUCCESS;
        break;
      case ActivationResult.kFailedToActivate:
        this.state_ = cellularSetup.PSimUIState.ACTIVATION_FAILURE;
        break;
      default:
        assertNotReached();
    }
  },

  /** @private */
  updateShowError_() {
    switch (this.state_) {
      case cellularSetup.PSimUIState.TIMEOUT_START_ACTIVATION:
      case cellularSetup.PSimUIState.TIMEOUT_PORTAL_LOAD:
      case cellularSetup.PSimUIState.TIMEOUT_FINISH_ACTIVATION:
      case cellularSetup.PSimUIState.ACTIVATION_FAILURE:
        this.showError_ = true;
        return;
      default:
        this.showError_ = false;
        return;
    }
  },

  /** @private */
  updateSelectedPage_() {
    switch (this.state_) {
      case cellularSetup.PSimUIState.IDLE:
      case cellularSetup.PSimUIState.STARTING_ACTIVATION:
      case cellularSetup.PSimUIState.WAITING_FOR_ACTIVATION_TO_START:
      case cellularSetup.PSimUIState.TIMEOUT_START_ACTIVATION:
        this.selectedPSimPageName_ = cellularSetup.PSimPageName.SIM_DETECT;
        return;
      case cellularSetup.PSimUIState.WAITING_FOR_PORTAL_TO_LOAD:
      case cellularSetup.PSimUIState.TIMEOUT_PORTAL_LOAD:
      case cellularSetup.PSimUIState.WAITING_FOR_USER_PAYMENT:
        this.selectedPSimPageName_ = cellularSetup.PSimPageName.PROVISIONING;
        return;
      case cellularSetup.PSimUIState.WAITING_FOR_ACTIVATION_TO_FINISH:
      case cellularSetup.PSimUIState.TIMEOUT_FINISH_ACTIVATION:
      case cellularSetup.PSimUIState.ACTIVATION_SUCCESS:
      case cellularSetup.PSimUIState.ALREADY_ACTIVATED:
      case cellularSetup.PSimUIState.ACTIVATION_FAILURE:
        this.selectedPSimPageName_ = cellularSetup.PSimPageName.FINAL;
        return;
      default:
        assertNotReached();
    }
  },

  /** @private */
  handlePSimUIStateChange_() {
    // Since the state has changed, the previous state did not time out, so
    // clear any active timeout.
    this.clearTimer_();

    // If the new state has an associated timeout, set it.
    const timeoutMs = cellularSetup.getTimeoutMsForPSimUIState(this.state_);
    if (timeoutMs !== null) {
      this.currentTimeoutId_ =
          setTimeout(this.onTimeout_.bind(this), timeoutMs);
    }

    if (this.state_ === cellularSetup.PSimUIState.STARTING_ACTIVATION) {
      this.startActivation_();
      return;
    }
  },

  /** @private */
  onTimeout_() {
    // The activation attempt failed, so close the connection to the service.
    this.closeActivationConnection_();

    switch (this.state_) {
      case cellularSetup.PSimUIState.STARTING_ACTIVATION:
        this.state_ = cellularSetup.PSimUIState.TIMEOUT_START_ACTIVATION;
        return;
      case cellularSetup.PSimUIState.WAITING_FOR_PORTAL_TO_LOAD:
        this.state_ = cellularSetup.PSimUIState.TIMEOUT_PORTAL_LOAD;
        return;
      case cellularSetup.PSimUIState.WAITING_FOR_ACTIVATION_TO_FINISH:
        this.state_ = cellularSetup.PSimUIState.TIMEOUT_FINISH_ACTIVATION;
        return;
      default:
        // Only the above states are expected to time out.
        assertNotReached();
    }
  },

  /** @private */
  startActivation_() {
    assert(!this.activationDelegateReceiver_);
    this.activationDelegateReceiver_ =
        new chromeos.cellularSetup.mojom.ActivationDelegateReceiver(
            /**
             * @type {!chromeos.cellularSetup.mojom.ActivationDelegateInterface}
             */
            (this));

    this.mojoInterfaceProvider_.getMojoServiceRemote()
        .startActivation(
            this.activationDelegateReceiver_.$.bindNewPipeAndPassRemote())
        .then(
            /**
             * @param {!chromeos.cellularSetup.
             *             mojom.CellularSetup_StartActivation_ResponseParams}
             *                 params
             */
            (params) => {
              this.carrierPortalHandler_ = params.observer;
            });
  },

  /** @private */
  closeActivationConnection_() {
    assert(!!this.activationDelegateReceiver_);
    this.activationDelegateReceiver_.$.close();
    this.activationDelegateReceiver_ = null;
    this.carrierPortalHandler_ = null;
    this.cellularMetadata_ = null;
  },

  /** @private */
  clearTimer_() {
    if (this.currentTimeoutId_) {
      clearTimeout(this.currentTimeoutId_);
    }
    this.currentTimeoutId_ = null;
  },

  /** @private */
  onCarrierPortalLoaded_() {
    this.state_ = cellularSetup.PSimUIState.WAITING_FOR_USER_PAYMENT;
    this.carrierPortalHandler_.onCarrierPortalStatusChange(
        chromeos.cellularSetup.mojom.CarrierPortalStatus
            .kPortalLoadedWithoutPaidUser);
  },

  /**
   * @param {!CustomEvent<boolean>} event
   * @private
   */
  onCarrierPortalResult_(event) {
    const success = event.detail;
    this.state_ = success ? cellularSetup.PSimUIState.ACTIVATION_SUCCESS :
                            cellularSetup.PSimUIState.ACTIVATION_FAILURE;
  },
});
