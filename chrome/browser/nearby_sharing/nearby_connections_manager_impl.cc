// Copyright 2020 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/nearby_sharing/nearby_connections_manager_impl.h"

#include "base/files/file_util.h"
#include "base/strings/string_number_conversions.h"
#include "base/task/post_task.h"
#include "base/unguessable_token.h"
#include "chrome/browser/nearby_sharing/logging/logging.h"
#include "chrome/services/sharing/public/mojom/nearby_connections_types.mojom.h"
#include "crypto/random.h"
#include "mojo/public/cpp/bindings/pending_remote.h"
#include "net/base/network_change_notifier.h"

namespace {

const char kServiceId[] = "NearbySharing";
const location::nearby::connections::mojom::Strategy kStrategy =
    location::nearby::connections::mojom::Strategy::kP2pPointToPoint;

bool ShouldEnableWebRtc(bool is_advertising,
                        DataUsage data_usage,
                        PowerLevel power_level) {
  // We won't use internet if the user requested we don't.
  if (data_usage == DataUsage::kOffline)
    return false;

  // We won't use internet in a low power mode.
  if (power_level == PowerLevel::kLowPower)
    return false;

  net::NetworkChangeNotifier::ConnectionType connection_type =
      net::NetworkChangeNotifier::GetConnectionType();

  // Verify that this network has an internet connection.
  if (connection_type == net::NetworkChangeNotifier::CONNECTION_NONE)
    return false;

  // If the user wants to limit WebRTC, then only use it on unmetered networks.
  if (data_usage == DataUsage::kWifiOnly)
    return !net::NetworkChangeNotifier::IsConnectionCellular(connection_type);

  // We're online, the user hasn't disabled WebRTC, let's use it!
  return true;
}

InitializeFileResult CreateAndOpenFile(base::FilePath file_path) {
  base::FilePath unique_path = base::GetUniquePath(file_path);
  InitializeFileResult result;
  result.output_file.Initialize(
      unique_path,
      base::File::Flags::FLAG_CREATE_ALWAYS | base::File::Flags::FLAG_WRITE);
  result.input_file.Initialize(
      unique_path, base::File::Flags::FLAG_OPEN | base::File::Flags::FLAG_READ);
  return result;
}

}  // namespace

NearbyConnectionsManagerImpl::NearbyConnectionsManagerImpl(
    NearbyProcessManager* process_manager,
    Profile* profile)
    : process_manager_(process_manager), profile_(profile) {
  DCHECK(process_manager_);
  DCHECK(profile_);
  nearby_process_observer_.Add(process_manager_);
}

NearbyConnectionsManagerImpl::~NearbyConnectionsManagerImpl() = default;

void NearbyConnectionsManagerImpl::Shutdown() {
  // TOOD(crbug/1076008): Implement.
  // Disconnects from all endpoints and shut down Nearby Connections.
  Reset();
}

void NearbyConnectionsManagerImpl::StartAdvertising(
    std::vector<uint8_t> endpoint_info,
    IncomingConnectionListener* listener,
    PowerLevel power_level,
    DataUsage data_usage,
    ConnectionsCallback callback) {
  DCHECK(listener);
  DCHECK(!incoming_connection_listener_);

  if (!BindNearbyConnections()) {
    std::move(callback).Run(ConnectionsStatus::kError);
    return;
  }

  bool is_high_power = power_level == PowerLevel::kHighPower;
  auto allowed_mediums = MediumSelection::New(
      /*bluetooth=*/is_high_power,
      ShouldEnableWebRtc(/*is_advertising=*/true, data_usage, power_level),
      /*wifi_lan=*/is_high_power);

  mojo::PendingRemote<ConnectionLifecycleListener> lifecycle_listener;
  connection_lifecycle_listeners_.Add(
      this, lifecycle_listener.InitWithNewPipeAndPassReceiver());

  incoming_connection_listener_ = listener;
  nearby_connections_->StartAdvertising(
      endpoint_info, kServiceId,
      AdvertisingOptions::New(kStrategy, std::move(allowed_mediums),
                              /*auto_upgrade_bandwidth=*/is_high_power,
                              /*enforce_topology_constraints=*/true),
      std::move(lifecycle_listener), std::move(callback));
}

void NearbyConnectionsManagerImpl::StopAdvertising() {
  if (nearby_connections_) {
    nearby_connections_->StopAdvertising(base::BindOnce([](ConnectionsStatus
                                                               status) {
      NS_LOG(VERBOSE)
          << __func__
          << ": Stop advertising attempted over Nearby Connections with result "
          << status;
    }));
  }

  incoming_connection_listener_ = nullptr;
}

void NearbyConnectionsManagerImpl::StartDiscovery(
    DiscoveryListener* listener,
    ConnectionsCallback callback) {
  DCHECK(listener);
  DCHECK(!discovery_listener_);

  if (!BindNearbyConnections()) {
    std::move(callback).Run(ConnectionsStatus::kError);
    return;
  }

  discovery_listener_ = listener;
  nearby_connections_->StartDiscovery(
      kServiceId, DiscoveryOptions::New(kStrategy),
      endpoint_discovery_listener_.BindNewPipeAndPassRemote(),
      std::move(callback));
}

void NearbyConnectionsManagerImpl::StopDiscovery() {
  if (nearby_connections_) {
    nearby_connections_->StopDiscovery(
        base::BindOnce([](ConnectionsStatus status) {
          NS_LOG(VERBOSE) << __func__
                          << ": Stop discovery attempted over Nearby "
                             "Connections with result "
                          << status;
        }));
  }

  discovered_endpoints_.clear();
  discovery_listener_ = nullptr;
  endpoint_discovery_listener_.reset();
}

void NearbyConnectionsManagerImpl::Connect(
    std::vector<uint8_t> endpoint_info,
    const std::string& endpoint_id,
    base::Optional<std::vector<uint8_t>> bluetooth_mac_address,
    DataUsage data_usage,
    NearbyConnectionCallback callback) {
  // TOOD(crbug/1076008): Implement.
  if (!nearby_connections_) {
    std::move(callback).Run(nullptr);
    return;
  }

  mojo::PendingRemote<ConnectionLifecycleListener> lifecycle_listener;
  connection_lifecycle_listeners_.Add(
      this, lifecycle_listener.InitWithNewPipeAndPassReceiver());

  // TODO(crbug/10706008): Add MediumSelector and bluetooth_mac_address.
  nearby_connections_->RequestConnection(
      endpoint_info, endpoint_id, std::move(lifecycle_listener),
      base::BindOnce(&NearbyConnectionsManagerImpl::OnConnectionRequested,
                     weak_ptr_factory_.GetWeakPtr(), endpoint_id,
                     std::move(callback)));
}

void NearbyConnectionsManagerImpl::OnConnectionRequested(
    const std::string& endpoint_id,
    NearbyConnectionCallback callback,
    ConnectionsStatus status) {
  if (status != ConnectionsStatus::kSuccess) {
    NS_LOG(ERROR) << "Failed to connect to the remote shareTarget: " << status;
    nearby_connections_->DisconnectFromEndpoint(
        endpoint_id,
        base::BindOnce(
            [](const std::string& endpoint_id, ConnectionsStatus status) {
              NS_LOG(VERBOSE)
                  << __func__ << ": Disconnecting from endpoint " << endpoint_id
                  << " attempted over Nearby Connections with result "
                  << status;
            },
            endpoint_id));
    std::move(callback).Run(nullptr);
    return;
  }

  auto result =
      pending_outgoing_connections_.emplace(endpoint_id, std::move(callback));
  DCHECK(result.second);

  // TODO(crbug/1111458): Support TransferManager.
}

void NearbyConnectionsManagerImpl::Disconnect(const std::string& endpoint_id) {
  if (!nearby_connections_)
    return;

  nearby_connections_->DisconnectFromEndpoint(
      endpoint_id,
      base::BindOnce(
          [](const std::string& endpoint_id, ConnectionsStatus status) {
            NS_LOG(VERBOSE)
                << __func__ << ": Disconnecting from endpoint " << endpoint_id
                << " attempted over Nearby Connections with result " << status;
          },
          endpoint_id));

  OnDisconnected(endpoint_id);
  NS_LOG(INFO) << "Disconnected from " << endpoint_id;
}

void NearbyConnectionsManagerImpl::Send(const std::string& endpoint_id,
                                        PayloadPtr payload,
                                        PayloadStatusListener* listener) {
  if (!nearby_connections_)
    return;

  if (listener)
    RegisterPayloadStatusListener(payload->id, listener);

  nearby_connections_->SendPayload(
      {endpoint_id}, std::move(payload),
      base::BindOnce(
          [](const std::string& endpoint_id, ConnectionsStatus status) {
            NS_LOG(VERBOSE)
                << __func__ << ": Sending payload to endpoint " << endpoint_id
                << " attempted over Nearby Connections with result " << status;
          },
          endpoint_id));
}

void NearbyConnectionsManagerImpl::RegisterPayloadStatusListener(
    int64_t payload_id,
    PayloadStatusListener* listener) {
  payload_status_listeners_.insert_or_assign(payload_id, listener);
}

void NearbyConnectionsManagerImpl::RegisterPayloadPath(
    int64_t payload_id,
    const base::FilePath& file_path,
    ConnectionsCallback callback) {
  if (!nearby_connections_)
    return;

  DCHECK(!file_path.empty());
  base::ThreadPool::PostTaskAndReplyWithResult(
      FROM_HERE, {base::MayBlock()},
      base::BindOnce(&CreateAndOpenFile, file_path),
      base::BindOnce(&NearbyConnectionsManagerImpl::OnFileInitialized,
                     weak_ptr_factory_.GetWeakPtr(), payload_id,
                     std::move(callback)));
}

void NearbyConnectionsManagerImpl::OnFileInitialized(
    int64_t payload_id,
    ConnectionsCallback callback,
    InitializeFileResult result) {
  nearby_connections_->RegisterPayloadFile(
      payload_id, std::move(result.input_file), std::move(result.output_file),
      std::move(callback));
}

NearbyConnectionsManagerImpl::Payload*
NearbyConnectionsManagerImpl::GetIncomingPayload(int64_t payload_id) {
  auto it = incoming_payloads_.find(payload_id);
  if (it == incoming_payloads_.end())
    return nullptr;

  return it->second.get();
}

void NearbyConnectionsManagerImpl::Cancel(int64_t payload_id) {
  if (!nearby_connections_)
    return;

  auto it = payload_status_listeners_.find(payload_id);
  if (it != payload_status_listeners_.end()) {
    it->second->OnStatusUpdate(
        PayloadTransferUpdate::New(payload_id, PayloadStatus::kCanceled,
                                   /*total_bytes=*/0,
                                   /*bytes_transferred=*/0));
    payload_status_listeners_.erase(it);
  }
  nearby_connections_->CancelPayload(
      payload_id, base::BindOnce(
                      [](int64_t payload_id, ConnectionsStatus status) {
                        NS_LOG(VERBOSE)
                            << __func__ << ": Cancelling payload to id "
                            << payload_id
                            << " attempted over Nearby Connections with result "
                            << status;
                      },
                      payload_id));
  NS_LOG(INFO) << "Cancelling payload: " << payload_id;
}

void NearbyConnectionsManagerImpl::ClearIncomingPayloads() {
  incoming_payloads_.clear();
}

base::Optional<std::vector<uint8_t>>
NearbyConnectionsManagerImpl::GetRawAuthenticationToken(
    const std::string& endpoint_id) {
  auto it = connection_info_map_.find(endpoint_id);
  if (it == connection_info_map_.end())
    return base::nullopt;

  return it->second->raw_authentication_token;
}

void NearbyConnectionsManagerImpl::UpgradeBandwidth(
    const std::string& endpoint_id) {
  if (!nearby_connections_)
    return;

  nearby_connections_->InitiateBandwidthUpgrade(
      endpoint_id,
      base::BindOnce(
          [](const std::string& endpoint_id, ConnectionsStatus status) {
            NS_LOG(VERBOSE)
                << __func__ << ": Bandwidth upgrade attempted to endpoint "
                << endpoint_id << "over Nearby Connections with result "
                << status;
          },
          endpoint_id));
}

void NearbyConnectionsManagerImpl::OnNearbyProfileChanged(Profile* profile) {
  NS_LOG(VERBOSE) << __func__;
}

void NearbyConnectionsManagerImpl::OnNearbyProcessStarted() {
  NS_LOG(VERBOSE) << __func__;
}

void NearbyConnectionsManagerImpl::OnNearbyProcessStopped() {
  NS_LOG(VERBOSE) << __func__;
  // Not safe to use nearby_connections after we are notified the process has
  // been stopped.
  nearby_connections_ = nullptr;
  Reset();
}

void NearbyConnectionsManagerImpl::OnEndpointFound(
    const std::string& endpoint_id,
    DiscoveredEndpointInfoPtr info) {
  if (!discovery_listener_) {
    NS_LOG(INFO) << "Ignoring discovered endpoint "
                 << base::HexEncode(info->endpoint_info.data(),
                                    info->endpoint_info.size())
                 << " because we're no longer "
                    "in discovery mode";
    return;
  }

  auto result = discovered_endpoints_.insert(endpoint_id);
  if (!result.second) {
    NS_LOG(INFO) << "Ignoring discovered endpoint "
                 << base::HexEncode(info->endpoint_info.data(),
                                    info->endpoint_info.size())
                 << " because we've already "
                    "reported this endpoint";
    return;
  }

  discovery_listener_->OnEndpointDiscovered(endpoint_id, info->endpoint_info);
  NS_LOG(INFO) << "Discovered "
               << base::HexEncode(info->endpoint_info.data(),
                                  info->endpoint_info.size())
               << " over Nearby Connections";
}

void NearbyConnectionsManagerImpl::OnEndpointLost(
    const std::string& endpoint_id) {
  if (!discovered_endpoints_.erase(endpoint_id)) {
    NS_LOG(INFO) << "Ignoring lost endpoint " << endpoint_id
                 << " because we haven't reported this endpoint";
    return;
  }

  if (!discovery_listener_) {
    NS_LOG(INFO) << "Ignoring lost endpoint " << endpoint_id
                 << " because we're no longer in discovery mode";
    return;
  }

  discovery_listener_->OnEndpointLost(endpoint_id);
  NS_LOG(INFO) << "Endpoint " << endpoint_id << " lost over Nearby Connections";
}

void NearbyConnectionsManagerImpl::OnConnectionInitiated(
    const std::string& endpoint_id,
    ConnectionInfoPtr info) {
  auto result = connection_info_map_.emplace(endpoint_id, std::move(info));
  DCHECK(result.second);

  mojo::PendingRemote<PayloadListener> payload_listener;
  payload_listeners_.Add(this,
                         payload_listener.InitWithNewPipeAndPassReceiver());

  nearby_connections_->AcceptConnection(
      endpoint_id, std::move(payload_listener),
      base::BindOnce(
          [](const std::string& endpoint_id, ConnectionsStatus status) {
            NS_LOG(VERBOSE)
                << __func__ << ": Accept connection attempted to endpoint "
                << endpoint_id << "over Nearby Connections with result "
                << status;
          },
          endpoint_id));
}

void NearbyConnectionsManagerImpl::OnConnectionAccepted(
    const std::string& endpoint_id) {
  auto it = connection_info_map_.find(endpoint_id);
  if (it == connection_info_map_.end())
    return;

  if (it->second->is_incoming_connection) {
    if (!incoming_connection_listener_) {
      // Not in advertising mode.
      Disconnect(endpoint_id);
      return;
    }

    auto result = connections_.emplace(
        endpoint_id, std::make_unique<NearbyConnectionImpl>(this, endpoint_id));
    DCHECK(result.second);
    incoming_connection_listener_->OnIncomingConnection(
        endpoint_id, it->second->endpoint_info, result.first->second.get());
  } else {
    auto it = pending_outgoing_connections_.find(endpoint_id);
    if (it == pending_outgoing_connections_.end()) {
      Disconnect(endpoint_id);
      return;
    }

    auto result = connections_.emplace(
        endpoint_id, std::make_unique<NearbyConnectionImpl>(this, endpoint_id));
    DCHECK(result.second);
    std::move(it->second).Run(result.first->second.get());
    pending_outgoing_connections_.erase(it);
  }
}

void NearbyConnectionsManagerImpl::OnConnectionRejected(
    const std::string& endpoint_id,
    Status status) {
  connection_info_map_.erase(endpoint_id);

  auto it = pending_outgoing_connections_.find(endpoint_id);
  if (it != pending_outgoing_connections_.end()) {
    std::move(it->second).Run(nullptr);
    pending_outgoing_connections_.erase(it);
  }

  // TODO(crbug/1111458): Support TransferManager.
}

void NearbyConnectionsManagerImpl::OnDisconnected(
    const std::string& endpoint_id) {
  connection_info_map_.erase(endpoint_id);

  auto it = pending_outgoing_connections_.find(endpoint_id);
  if (it != pending_outgoing_connections_.end()) {
    std::move(it->second).Run(nullptr);
    pending_outgoing_connections_.erase(it);
  }

  connections_.erase(endpoint_id);

  // TODO(crbug/1111458): Support TransferManager.
}

void NearbyConnectionsManagerImpl::OnBandwidthChanged(
    const std::string& endpoint_id,
    int32_t quality) {
  NS_LOG(VERBOSE) << __func__;
  // TODO(crbug/1111458): Support TransferManager.
}

void NearbyConnectionsManagerImpl::OnPayloadReceived(
    const std::string& endpoint_id,
    PayloadPtr payload) {
  auto result = incoming_payloads_.emplace(payload->id, std::move(payload));
  DCHECK(result.second);
}

void NearbyConnectionsManagerImpl::OnPayloadTransferUpdate(
    const std::string& endpoint_id,
    PayloadTransferUpdatePtr update) {
  // If this is a payload we've registered for, then forward its status to the
  // PayloadStatusListener. We don't need to do anything more with the payload.
  auto listener_it = payload_status_listeners_.find(update->payload_id);
  if (listener_it != payload_status_listeners_.end()) {
    PayloadStatusListener* listener = listener_it->second;
    switch (update->status) {
      case PayloadStatus::kInProgress:
        break;
      case PayloadStatus::kSuccess:
      case PayloadStatus::kCanceled:
      case PayloadStatus::kFailure:
        payload_status_listeners_.erase(listener_it);
        break;
    }
    listener->OnStatusUpdate(std::move(update));
    return;
  }

  // If this is an incoming payload that we have not registered for, then we'll
  // treat it as a control frame (eg. IntroductionFrame) and forward it to the
  // associated NearbyConnection.
  auto payload_it = incoming_payloads_.find(update->payload_id);
  if (payload_it == incoming_payloads_.end())
    return;

  if (!payload_it->second->content->is_bytes()) {
    NS_LOG(WARNING) << "Received unknown payload of file type. Cancelling.";
    nearby_connections_->CancelPayload(payload_it->first, base::DoNothing());
    return;
  }

  if (update->status != PayloadStatus::kSuccess)
    return;

  auto connections_it = connections_.find(endpoint_id);
  if (connections_it == connections_.end())
    return;

  NS_LOG(INFO) << "Writing incoming byte message to NearbyConnection.";
  connections_it->second->WriteMessage(
      payload_it->second->content->get_bytes()->bytes);
}

bool NearbyConnectionsManagerImpl::BindNearbyConnections() {
  if (!nearby_connections_) {
    nearby_connections_ =
        process_manager_->GetOrStartNearbyConnections(profile_);
  }
  return nearby_connections_ != nullptr;
}

void NearbyConnectionsManagerImpl::Reset() {
  if (nearby_connections_) {
    nearby_connections_->StopAllEndpoints(
        base::BindOnce([](ConnectionsStatus status) {
          NS_LOG(VERBOSE) << __func__
                          << ": Stop all endpoints attempted over Nearby "
                             "Connections with result "
                          << status;
        }));
  }
  nearby_connections_ = nullptr;
  discovered_endpoints_.clear();
  discovery_listener_ = nullptr;
  incoming_connection_listener_ = nullptr;
  endpoint_discovery_listener_.reset();
}
