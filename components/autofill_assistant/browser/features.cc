// Copyright 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "components/autofill_assistant/browser/features.h"

#include "base/feature_list.h"

namespace autofill_assistant {
namespace features {

const base::Feature kAutofillAssistant{"AutofillAssistant",
                                       base::FEATURE_ENABLED_BY_DEFAULT};

// Controls whether to enable Assistant Autofill in a normal Chrome tab.
const base::Feature kAutofillAssistantChromeEntry{
    "AutofillAssistantChromeEntry", base::FEATURE_ENABLED_BY_DEFAULT};

const base::Feature kAutofillAssistantDirectActions{
    "AutofillAssistantDirectActions", base::FEATURE_ENABLED_BY_DEFAULT};

const base::Feature kAutofillAssistantProactiveHelp{
    "AutofillAssistantProactiveHelp", base::FEATURE_DISABLED_BY_DEFAULT};

// Use Chrome's TabHelper system to deal with the life cycle of WebContent's
// depending Autofill Assistant objects.
const base::Feature kAutofillAssistantWithTabHelper{
    "AutofillAssistantWithTabHelper", base::FEATURE_DISABLED_BY_DEFAULT};

// By default, proactive help is only offered if MSBB is turned on. This feature
// flag allows disabling the link. Proactive help can still be offered to users
// so long as no communication to a remote backend is required. Specifically,
// base64-injected trigger scripts can be shown even in the absence of MSBB.
const base::Feature kAutofillAssistantDisableProactiveHelpTiedToMSBB{
    "AutofillAssistantDisableProactiveHelpTiedToMSBB",
    base::FEATURE_DISABLED_BY_DEFAULT};

}  // namespace features
}  // namespace autofill_assistant
