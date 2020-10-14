// Copyright 2020 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

/**
 * @fileoverview The 'nearby-discovery-page' component shows the discovery UI of
 * the Nearby Share flow. It shows a list of devices to select from.
 */

import 'chrome://resources/cr_elements/cr_button/cr_button.m.js';
import 'chrome://resources/mojo/mojo/public/js/mojo_bindings_lite.js';
import 'chrome://resources/mojo/mojo/public/mojom/base/unguessable_token.mojom-lite.js';
import 'chrome://resources/polymer/v3_0/iron-list/iron-list.js';
import './nearby_device.js';
import './nearby_preview.js';
import './nearby_share_target_types.mojom-lite.js';
import './nearby_share.mojom-lite.js';

import {assert} from 'chrome://resources/js/assert.m.js';
import {html, Polymer} from 'chrome://resources/polymer/v3_0/polymer/polymer_bundled.min.js';

import {getDiscoveryManager} from './discovery_manager.js';

/**
 * Converts an unguessable token to a string.
 * @param {!mojoBase.mojom.UnguessableToken} token
 * @return {!string}
 */
function tokenToString(token) {
  return `${token.high.toString()}#${token.low.toString()}`;
}

/**
 * Compares two unguessable tokens.
 * @param {!mojoBase.mojom.UnguessableToken} a
 * @param {!mojoBase.mojom.UnguessableToken} b
 */
function tokensEqual(a, b) {
  return a.high === b.high && a.low === b.low;
}

Polymer({
  is: 'nearby-discovery-page',

  _template: html`{__html_template__}`,

  properties: {
    /**
     * ConfirmationManager interface for the currently selected share target.
     * @type {?nearbyShare.mojom.ConfirmationManagerInterface}
     */
    confirmationManager: {
      notify: true,
      type: Object,
      value: null,
    },

    /**
     * Token to show to the user to confirm the selected share target.
     * @type {?string}
     */
    confirmationToken: {
      notify: true,
      type: String,
      value: null,
    },

    /**
     * The currently selected share target.
     * @type {?nearbyShare.mojom.ShareTarget}
     */
    selectedShareTarget: {
      notify: true,
      type: Object,
      value: null,
    },

    /**
     * A list of all discovered nearby share targets.
     * @private {!Array<!nearbyShare.mojom.ShareTarget>}
     */
    shareTargets_: {
      type: Array,
      value: [],
    },
  },

  /** @private {nearbyShare.mojom.ShareTargetListenerCallbackRouter} */
  mojoEventTarget_: null,

  /** @private {Array<number>} */
  listenerIds_: null,

  /** @private {Map<!string,!nearbyShare.mojom.ShareTarget>} */
  shareTargetMap_: null,

  /** @private {?nearbyShare.mojom.ShareTarget} */
  lastSelectedShareTarget_: null,

  /** @override */
  attached() {
    // TODO(knollr): Remove this once prototyping is done.
    this.shareTargets_ = [
      {
        id: {high: 0, low: 1},
        name: 'Alyssa\'s Pixel',
        type: nearbyShare.mojom.ShareTargetType.kTablet,
      },
      {
        id: {high: 0, low: 2},
        name: 'Shangela\'s Pixel 2XL',
        type: nearbyShare.mojom.ShareTargetType.kPhone,
      },
      {
        id: {high: 0, low: 3},
        name: 'Mira\'s Chromebook',
        type: nearbyShare.mojom.ShareTargetType.kLaptop,
      }
    ];
    this.shareTargetMap_ = new Map();

    this.mojoEventTarget_ =
        new nearbyShare.mojom.ShareTargetListenerCallbackRouter();

    this.listenerIds_ = [
      this.mojoEventTarget_.onShareTargetDiscovered.addListener(
          this.onShareTargetDiscovered_.bind(this)),
      this.mojoEventTarget_.onShareTargetLost.addListener(
          this.onShareTargetLost_.bind(this)),
    ];

    // TODO(knollr): Only do this when the discovery page is actually shown.
    getDiscoveryManager()
        .startDiscovery(this.mojoEventTarget_.$.bindNewPipeAndPassRemote())
        .then(response => {
          if (!response.success) {
            // TODO(knollr): Show error.
            return;
          }
        });
  },

  /** @override */
  detached() {
    this.listenerIds_.forEach(
        id => assert(this.mojoEventTarget_.removeListener(id)));
    this.mojoEventTarget_.$.close();
  },

  /**
   * @private
   * @param {!nearbyShare.mojom.ShareTarget} shareTarget The discovered device.
   */
  onShareTargetDiscovered_(shareTarget) {
    const shareTargetId = tokenToString(shareTarget.id);
    if (!this.shareTargetMap_.has(shareTargetId)) {
      this.push('shareTargets_', shareTarget);
    } else {
      const index = this.shareTargets_.findIndex(
          (target) => tokensEqual(target.id, shareTarget.id));
      assert(index !== -1);
      this.splice('shareTargets_', index, 1, shareTarget);
      this.updateSelectedShareTarget_(shareTarget.id, shareTarget);
    }
    this.shareTargetMap_.set(shareTargetId, shareTarget);
  },

  /**
   * @private
   * @param {!nearbyShare.mojom.ShareTarget} shareTarget The lost device.
   */
  onShareTargetLost_(shareTarget) {
    const index = this.shareTargets_.findIndex(
        (target) => tokensEqual(target.id, shareTarget.id));
    assert(index !== -1);
    this.splice('shareTargets_', index, 1);
    this.shareTargetMap_.delete(tokenToString(shareTarget.id));
    this.updateSelectedShareTarget_(shareTarget.id, /*shareTarget=*/ null);
  },

  /** @private */
  onNextTap_() {
    if (!this.selectedShareTarget) {
      return;
    }

    getDiscoveryManager()
        .selectShareTarget(this.selectedShareTarget.id)
        .then(response => {
          const {result, token, confirmationManager} = response;
          if (result !== nearbyShare.mojom.SelectShareTargetResult.kOk) {
            // TODO(knollr): Show error.
            return;
          }

          if (confirmationManager) {
            this.confirmationManager = confirmationManager;
            this.confirmationToken = token;
            this.fire('change-page', {page: 'confirmation'});
          } else {
            // TODO(knollr): Close dialog as send is now in progress.
          }
        });
  },

  /** @private */
  onSelectedShareTargetChanged_() {
    // <iron-list> causes |this.$.deviceList.selectedItem| to be null if tapped
    // a second time. Manually reselect the last item to preserve selection.
    if (!this.$.deviceList.selectedItem && this.lastSelectedShareTarget_) {
      this.$.deviceList.selectItem(this.lastSelectedShareTarget_);
    }
    this.lastSelectedShareTarget_ = this.$.deviceList.selectedItem;
  },

  /**
   * @param {!nearbyShare.mojom.ShareTarget} shareTarget
   * @return {boolean}
   * @private
   */
  isShareTargetSelected_(shareTarget) {
    return this.selectedShareTarget === shareTarget;
  },

  /**
   * Updates the selected share tagrget to |shareTarget| if its id matches |id|.
   * @param {!mojoBase.mojom.UnguessableToken} id
   * @param {?nearbyShare.mojom.ShareTarget} shareTarget
   * @private
   */
  updateSelectedShareTarget_(id, shareTarget) {
    if (this.selectedShareTarget &&
        tokensEqual(this.selectedShareTarget.id, id)) {
      this.lastSelectedShareTarget_ = shareTarget;
      this.selectedShareTarget = shareTarget;
    }
  }
});
