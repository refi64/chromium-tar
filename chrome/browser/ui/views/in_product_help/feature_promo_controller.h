// Copyright 2020 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_VIEWS_IN_PRODUCT_HELP_FEATURE_PROMO_CONTROLLER_H_
#define CHROME_BROWSER_UI_VIEWS_IN_PRODUCT_HELP_FEATURE_PROMO_CONTROLLER_H_

#include "base/memory/weak_ptr.h"

struct FeaturePromoBubbleParams;

namespace base {
struct Feature;
}

// Manages display of in-product help promos. All IPH displays in Top
// Chrome should go through here.
class FeaturePromoController {
 public:
  FeaturePromoController() = default;
  virtual ~FeaturePromoController() = default;

  // Starts the promo if possible. Returns whether it started.
  // |iph_feature| must be an IPH feature defined in
  // components/feature_engagement/public/feature_list.cc. Note that
  // this is different than the feature that the IPH is for.
  virtual bool MaybeShowPromo(const base::Feature& iph_feature,
                              FeaturePromoBubbleParams params) = 0;

  // Returns whether a bubble is showing for the given IPH. Note that if
  // this is false, a promo might still be in progress; for example, a
  // promo may have continued into a menu in which case the bubble is no
  // longer showing.
  virtual bool BubbleIsShowing(const base::Feature& iph_feature) const = 0;

  // Close the bubble for |iph_feature| and end the promo. If no promo
  // is showing for |iph_feature|, or the promo has continued past the
  // bubble, calling this is an error.
  virtual void CloseBubble(const base::Feature& iph_feature) = 0;

  class PromoHandle;

  // Like CloseBubble() but does not end the promo yet. The caller takes
  // ownership of the promo (e.g. to show a highlight in a menu or on a
  // button). The returned PromoHandle represents this ownership.
  virtual PromoHandle CloseBubbleAndContinuePromo(
      const base::Feature& iph_feature) = 0;

  // When a caller wants to take ownership of the promo after a bubble
  // is closed, this handle is given. It must be dropped in a timely
  // fashion to ensure everything is cleaned up. If it isn't, it will
  // make the IPH backend think it's still shwoing and block all other
  // IPH indefinitely.
  class PromoHandle {
   public:
    explicit PromoHandle(base::WeakPtr<FeaturePromoController> controller);
    PromoHandle(PromoHandle&&);
    ~PromoHandle();

    PromoHandle& operator=(PromoHandle&&);

   private:
    base::WeakPtr<FeaturePromoController> controller_;
  };

 protected:
  // Called when PromoHandle is destroyed to finish the promo.
  virtual void FinishContinuedPromo() = 0;
};

#endif  // CHROME_BROWSER_UI_VIEWS_IN_PRODUCT_HELP_FEATURE_PROMO_CONTROLLER_H_
