// Copyright 2020 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/chromeos/net/network_health/network_health_localized_strings.h"

#include "chrome/browser/ui/webui/webui_util.h"
#include "chrome/grit/generated_resources.h"
#include "ui/base/webui/web_ui_util.h"

namespace chromeos {
namespace network_health {

namespace {

constexpr webui::LocalizedString kLocalizedStrings[] = {
    // Network Health Summary Strings
    {"NetworkHealthState", IDS_NETWORK_HEALTH_STATE},
    {"NetworkHealthStateUninitialized", IDS_NETWORK_HEALTH_STATE_UNINITIALIZED},
    {"NetworkHealthStateDisabled", IDS_NETWORK_HEALTH_STATE_DISABLED},
    {"NetworkHealthStateProhibited", IDS_NETWORK_HEALTH_STATE_PROHIBITED},
    {"NetworkHealthStateNotConnected", IDS_NETWORK_HEALTH_STATE_NOT_CONNECTED},
    {"NetworkHealthStateConnecting", IDS_NETWORK_HEALTH_STATE_CONNECTING},
    {"NetworkHealthStatePortal", IDS_NETWORK_HEALTH_STATE_PORTAL},
    {"NetworkHealthStateConnected", IDS_NETWORK_HEALTH_STATE_CONNECTED},
    {"NetworkHealthStateOnline", IDS_NETWORK_HEALTH_STATE_ONLINE},

    // Network Diagnostics Strings
    {"NetworkDiagnosticsLanConnectivity",
     IDS_NETWORK_DIAGNOSTICS_LAN_CONNECTIVITY},
    {"NetworkDiagnosticsSignalStrength",
     IDS_NETWORK_DIAGNOSTICS_SIGNAL_STRENGTH},
    {"NetworkDiagnosticsGatewayCanBePinged",
     IDS_NETWORK_DIAGNOSTICS_GATEWAY_CAN_BE_PINGED},
    {"NetworkDiagnosticsHasSecureWiFiConnection",
     IDS_NETWORK_DIAGNOSTICS_HAS_SECURE_WIFI_CONNECTION},
    {"NetworkDiagnosticsDnsResolverPresent",
     IDS_NETWORK_DIAGNOSTICS_DNS_RESOLVER_PRESENT},
    {"NetworkDiagnosticsDnsLatency", IDS_NETWORK_DIAGNOSTICS_DNS_LATENCY},
    {"NetworkDiagnosticsDnsResolution", IDS_NETWORK_DIAGNOSTICS_DNS_RESOLUTION},
    {"NetworkDiagnosticsPassed", IDS_NETWORK_DIAGNOSTICS_PASSED},
    {"NetworkDiagnosticsFailed", IDS_NETWORK_DIAGNOSTICS_FAILED},
    {"NetworkDiagnosticsNotRun", IDS_NETWORK_DIAGNOSTICS_NOT_RUN},
    {"NetworkDiagnosticsRun", IDS_NETWORK_DIAGNOSTICS_RUN},
    {"NetworkDiagnosticsRunAll", IDS_NETWORK_DIAGNOSTICS_RUN_ALL},
    {"NetworkDiagnosticsSendFeedback", IDS_NETWORK_DIAGNOSTICS_SEND_FEEDBACK},
    {"SignalStrengthProblem_NotFound",
     IDS_NETWORK_DIAGNOSTICS_SIGNAL_STRENGTH_PROBLEM_NOT_FOUND},
    {"SignalStrengthProblem_Weak",
     IDS_NETWORK_DIAGNOSTICS_SIGNAL_STRENGTH_PROBLEM_WEAK},
    {"GatewayPingProblem_Unreachable",
     IDS_NETWORK_DIAGNOSTICS_GATEWAY_CAN_BE_PINGED_PROBLEM_UNREACHABLE},
    {"GatewayPingProblem_NoDefaultPing",
     IDS_NETWORK_DIAGNOSTICS_GATEWAY_CAN_BE_PINGED_PROBLEM_PING_DEFAULT_FAILED},
    {"GatewayPingProblem_DefaultLatency",
     IDS_NETWORK_DIAGNOSTICS_GATEWAY_CAN_BE_PINGED_PROBLEM_DEFAULT_ABOVE_LATENCY},
    {"GatewayPingProblem_NoNonDefaultPing",
     IDS_NETWORK_DIAGNOSTICS_GATEWAY_CAN_BE_PINGED_PROBLEM_PING_NON_DEFAULT_FAILED},
    {"GatewayPingProblem_NonDefaultLatency",
     IDS_NETWORK_DIAGNOSTICS_GATEWAY_CAN_BE_PINGED_PROBLEM_NON_DEFAULT_ABOVE_LATENCY},
    {"SecureWifiProblem_None",
     IDS_NETWORK_DIAGNOSTICS_SECURE_WIFI_PROBLEM_NOT_SECURE},
    {"SecureWifiProblem_8021x",
     IDS_NETWORK_DIAGNOSTICS_SECURE_WIFI_PROBLEM_WEP_8021x},
    {"SecureWifiProblem_PSK",
     IDS_NETWORK_DIAGNOSTICS_SECURE_WIFI_PROBLEM_WEP_PSK},
    {"SecureWifiProblem_Unknown",
     IDS_NETWORK_DIAGNOSTICS_SECURE_WIFI_PROBLEM_UNKNOWN},
    {"DnsResolverProblem_NoNameServers",
     IDS_NETWORK_DIAGNOSTICS_DNS_RESOLVER_PROBLEM_NO_NAME_SERVERS},
    {"DnsResolverProblem_MalformedNameServers",
     IDS_NETWORK_DIAGNOSTICS_DNS_RESOLVER_PROBLEM_MALFORMED_NAME_SERVERS},
    {"DnsResolverProblem_EmptyNameServers",
     IDS_NETWORK_DIAGNOSTICS_DNS_RESOLVER_PROBLEM_EMPTY_NAME_SERVERS},
    {"DnsLatencyProblem_FailedResolveHosts",
     IDS_NETWORK_DIAGNOSTICS_DNS_LATENCY_PROBLEM_FAILED_TO_RESOLVE_ALL_HOSTS},
    {"DnsLatencyProblem_LatencySlightlyAbove",
     IDS_NETWORK_DIAGNOSTICS_DNS_LATENCY_PROBLEM_SLIGHTLY_ABOVE_THRESHOLD},
    {"DnsLatencyProblem_LatencySignificantlyAbove",
     IDS_NETWORK_DIAGNOSTICS_DNS_LATENCY_PROBLEM_SIGNIFICANTLY_ABOVE_THRESHOLD},
    {"DnsResolutionProblem_FailedResolve",
     IDS_NETWORK_DIAGNOSTICS_DNS_RESOLUTION_PROBLEM_FAILED_TO_RESOLVE_HOST},
};

}  // namespace

void AddLocalizedStrings(content::WebUIDataSource* html_source) {
  AddLocalizedStringsBulk(html_source, kLocalizedStrings);
}

}  // namespace network_health
}  // namespace chromeos
