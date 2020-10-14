// Copyright 2020 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.components.messages;

import static org.chromium.components.messages.MessageBannerProperties.DESCRIPTION;
import static org.chromium.components.messages.MessageBannerProperties.ICON;
import static org.chromium.components.messages.MessageBannerProperties.PRIMARY_BUTTON_CLICK_LISTENER;
import static org.chromium.components.messages.MessageBannerProperties.PRIMARY_BUTTON_TEXT;
import static org.chromium.components.messages.MessageBannerProperties.SECONDARY_ICON;
import static org.chromium.components.messages.MessageBannerProperties.SECONDARY_ICON_CONTENT_DESCRIPTION;
import static org.chromium.components.messages.MessageBannerProperties.TITLE;

import org.chromium.ui.modelutil.PropertyKey;
import org.chromium.ui.modelutil.PropertyModel;

/**
 * View binder of Message banner.
 */
public class MessageBannerViewBinder {
    public static void bind(PropertyModel model, MessageBannerView view, PropertyKey propertyKey) {
        if (propertyKey == PRIMARY_BUTTON_TEXT) {
            view.setPrimaryButtonText(model.get(PRIMARY_BUTTON_TEXT));
        } else if (propertyKey == PRIMARY_BUTTON_CLICK_LISTENER) {
            view.setPrimaryButtonClickListener(model.get(PRIMARY_BUTTON_CLICK_LISTENER));
        } else if (propertyKey == TITLE) {
            view.setTitle(model.get(TITLE));
        } else if (propertyKey == DESCRIPTION) {
            view.setDescription(model.get(DESCRIPTION));
        } else if (propertyKey == ICON) {
            view.setIcon(model.get(ICON));
        } else if (propertyKey == SECONDARY_ICON) {
            view.setSecondaryIcon(model.get(SECONDARY_ICON));
        } else if (propertyKey == SECONDARY_ICON_CONTENT_DESCRIPTION) {
            view.setSecondaryIconContentDescription(model.get(SECONDARY_ICON_CONTENT_DESCRIPTION));
        }
    }
}
