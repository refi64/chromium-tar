// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/dns/dns_config_service_posix.h"

#include <memory>
#include <string>
#include <type_traits>

#include "base/bind.h"
#include "base/files/file.h"
#include "base/files/file_path.h"
#include "base/files/file_path_watcher.h"
#include "base/lazy_instance.h"
#include "base/location.h"
#include "base/logging.h"
#include "base/sequence_checker.h"
#include "base/single_thread_task_runner.h"
#include "base/threading/scoped_blocking_call.h"
#include "base/time/time.h"
#include "build/build_config.h"
#include "net/base/ip_address.h"
#include "net/base/ip_endpoint.h"
#include "net/dns/dns_config.h"
#include "net/dns/dns_hosts.h"
#include "net/dns/notify_watcher_mac.h"
#include "net/dns/public/dns_protocol.h"
#include "net/dns/serial_worker.h"

#if defined(OS_MAC)
#include "net/dns/dns_config_watcher_mac.h"
#endif

#if defined(OS_ANDROID)
#include <sys/system_properties.h>
#include "base/android/build_info.h"
#include "net/android/network_library.h"
#include "net/base/address_tracker_linux.h"
#include "net/base/network_change_notifier.h"
#include "net/base/network_interfaces.h"
#endif

namespace net {

namespace internal {

namespace {

#if defined(OS_ANDROID)
const base::FilePath::CharType kFilePathHosts[] =
    FILE_PATH_LITERAL("/system/etc/hosts");
#else
const base::FilePath::CharType kFilePathHosts[] =
    FILE_PATH_LITERAL("/etc/hosts");
#endif

#if defined(OS_IOS)
// There is no public API to watch the DNS configuration on iOS.
class DnsConfigWatcher {
 public:
  using CallbackType = base::RepeatingCallback<void(bool succeeded)>;

  bool Watch(const CallbackType& callback) {
    return false;
  }
};

#elif defined(OS_ANDROID)

// On Android, assume DNS config may have changed on every network change.
class DnsConfigWatcher : public NetworkChangeNotifier::NetworkChangeObserver {
 public:
  DnsConfigWatcher() { NetworkChangeNotifier::AddNetworkChangeObserver(this); }

  ~DnsConfigWatcher() override {
    NetworkChangeNotifier::RemoveNetworkChangeObserver(this);
  }

  using CallbackType = base::RepeatingCallback<void(bool succeeded)>;

  bool Watch(const CallbackType& callback) {
    callback_ = callback;
    return true;
  }

  // NetworkChangeNotifier::NetworkChangeObserver implementation:
  void OnNetworkChanged(NetworkChangeNotifier::ConnectionType type) override {
    if (!callback_.is_null() && type != NetworkChangeNotifier::CONNECTION_NONE)
      callback_.Run(true);
  }

 private:
  CallbackType callback_;
};

#elif defined(OS_MAC)

// DnsConfigWatcher for OS_MAC is in dns_config_watcher_mac.{hh,cc}.

#else  // !defined(OS_IOS) && !defined(OS_ANDROID) && !defined(OS_MAC)

#ifndef _PATH_RESCONF  // Normally defined in <resolv.h>
#define _PATH_RESCONF "/etc/resolv.conf"
#endif

const base::FilePath::CharType kFilePathConfig[] =
    FILE_PATH_LITERAL(_PATH_RESCONF);

class DnsConfigWatcher {
 public:
  using CallbackType = base::RepeatingCallback<void(bool succeeded)>;

  bool Watch(const CallbackType& callback) {
    callback_ = callback;
    return watcher_.Watch(base::FilePath(kFilePathConfig),
                          base::FilePathWatcher::Type::kNonRecursive,
                          base::BindRepeating(&DnsConfigWatcher::OnCallback,
                                              base::Unretained(this)));
  }

 private:
  void OnCallback(const base::FilePath& path, bool error) {
    callback_.Run(!error);
  }

  base::FilePathWatcher watcher_;
  CallbackType callback_;
};
#endif  // defined(OS_IOS)

#if defined(OS_ANDROID)
bool IsVpnPresent() {
  NetworkInterfaceList networks;
  if (!GetNetworkList(&networks, INCLUDE_HOST_SCOPE_VIRTUAL_INTERFACES))
    return false;

  for (NetworkInterface network : networks) {
    if (AddressTrackerLinux::IsTunnelInterfaceName(network.name.c_str()))
      return true;
  }
  return false;
}
#endif  // defined(OS_ANDROID)

bool ReadDnsConfig(DnsConfig* dns_config) {
  base::ScopedBlockingCall scoped_blocking_call(FROM_HERE,
                                                base::BlockingType::MAY_BLOCK);
  dns_config->unhandled_options = false;
#if !defined(OS_ANDROID)
  bool success = false;
// TODO(fuchsia): Use res_ninit() when it's implemented on Fuchsia.
#if defined(OS_OPENBSD) || defined(OS_FUCHSIA)
  // Note: res_ninit in glibc always returns 0 and sets RES_INIT.
  // res_init behaves the same way.
  memset(&_res, 0, sizeof(_res));
  if (res_init() == 0)
    success = ConvertResStateToDnsConfig(_res, dns_config);
#else  // all other OS_POSIX
  struct __res_state res;
  memset(&res, 0, sizeof(res));
  if (res_ninit(&res) == 0)
    success = ConvertResStateToDnsConfig(res, dns_config);
    // Prefer res_ndestroy where available.
#if defined(OS_APPLE) || defined(OS_FREEBSD)
  res_ndestroy(&res);
#else
  res_nclose(&res);
#endif  // defined(OS_APPLE) || defined(OS_FREEBSD)
#endif  // defined(OS_OPENBSD)

#if defined(OS_MAC)
  if (!DnsConfigWatcher::CheckDnsConfig(&dns_config->unhandled_options))
    return false;
#endif  // defined(OS_MAC)
  // Override |fallback_period| value to match default setting on Windows.
  dns_config->fallback_period = kDnsDefaultFallbackPeriod;
  return success;
#else  // defined(OS_ANDROID)
  dns_config->nameservers.clear();

  if (base::android::BuildInfo::GetInstance()->sdk_int() >=
      base::android::SDK_VERSION_MARSHMALLOW) {
    return net::android::GetDnsServers(&dns_config->nameservers,
                                       &dns_config->dns_over_tls_active,
                                       &dns_config->dns_over_tls_hostname);
  }

  if (IsVpnPresent()) {
    dns_config->unhandled_options = true;
    return true;
  }

  // NOTE(pauljensen): __system_property_get and the net.dns1/2 properties are
  // not supported APIs, but they're only read on pre-Marshmallow Android which
  // was released years ago and isn't changing.
  char property_value[PROP_VALUE_MAX];
  __system_property_get("net.dns1", property_value);
  std::string dns1_string = property_value;
  __system_property_get("net.dns2", property_value);
  std::string dns2_string = property_value;
  if (dns1_string.empty() && dns2_string.empty())
    return false;

  IPAddress dns1_address;
  IPAddress dns2_address;
  bool parsed1 = dns1_address.AssignFromIPLiteral(dns1_string);
  bool parsed2 = dns2_address.AssignFromIPLiteral(dns2_string);
  if (!parsed1 && !parsed2)
    return false;

  if (parsed1) {
    IPEndPoint dns1(dns1_address, dns_protocol::kDefaultPort);
    dns_config->nameservers.push_back(dns1);
  }
  if (parsed2) {
    IPEndPoint dns2(dns2_address, dns_protocol::kDefaultPort);
    dns_config->nameservers.push_back(dns2);
  }

  return true;
#endif  // !defined(OS_ANDROID)
}

}  // namespace

class DnsConfigServicePosix::Watcher : public DnsConfigService::Watcher {
 public:
  explicit Watcher(DnsConfigServicePosix* service)
      : DnsConfigService::Watcher(service) {}
  ~Watcher() override = default;

  bool Watch() override {
    CheckOnCorrectSequence();

    bool success = true;
    if (!config_watcher_.Watch(base::BindRepeating(&Watcher::OnConfigChanged,
                                                   base::Unretained(this)))) {
      LOG(ERROR) << "DNS config watch failed to start.";
      success = false;
    }
// Hosts file should never change on Android or iOS (and watching it on Android
// is problematic; see http://crbug.com/600442), so don't watch it there.
#if !defined(OS_ANDROID) && !defined(OS_IOS)
    if (!hosts_watcher_.Watch(
            base::FilePath(kFilePathHosts),
            base::FilePathWatcher::Type::kNonRecursive,
            base::BindRepeating(&Watcher::OnHostsFilePathWatcherChange,
                                base::Unretained(this)))) {
      LOG(ERROR) << "DNS hosts watch failed to start.";
      success = false;
    }
#endif  // !defined(OS_ANDROID) && !defined(OS_IOS)
    return success;
  }

 private:
#if !defined(OS_ANDROID) && !defined(OS_IOS)
  void OnHostsFilePathWatcherChange(const base::FilePath& path, bool error) {
    OnHostsChanged(!error);
  }
#endif  // !defined(OS_ANDROID) && !defined(OS_IOS)

  DnsConfigWatcher config_watcher_;
#if !defined(OS_ANDROID) && !defined(OS_IOS)
  base::FilePathWatcher hosts_watcher_;
#endif  // !defined(OS_ANDROID) && !defined(OS_IOS)

  DISALLOW_COPY_AND_ASSIGN(Watcher);
};

// A SerialWorker that uses libresolv to initialize res_state and converts
// it to DnsConfig (except on Android, where it reads system properties
// net.dns1 and net.dns2; see #if around ReadDnsConfig above.)
class DnsConfigServicePosix::ConfigReader : public SerialWorker {
 public:
  explicit ConfigReader(DnsConfigServicePosix* service)
      : service_(service), success_(false) {
    // Allow execution on another thread; nothing thread-specific about
    // constructor.
    DETACH_FROM_SEQUENCE(sequence_checker_);
  }

  void DoWork() override { success_ = ReadDnsConfig(&dns_config_); }

  void OnWorkFinished() override {
    DCHECK(!IsCancelled());
    if (success_) {
      service_->OnConfigRead(dns_config_);
    } else {
      LOG(WARNING) << "Failed to read DnsConfig.";
    }
  }

 private:
  ~ConfigReader() override = default;

  // Raw pointer to owning DnsConfigService. This must never be accessed inside
  // DoWork(), since service may be destroyed while SerialWorker is running
  // on worker thread.
  DnsConfigServicePosix* const service_;
  // Written in DoWork, read in OnWorkFinished, no locking necessary.
  DnsConfig dns_config_;
  bool success_;

  DISALLOW_COPY_AND_ASSIGN(ConfigReader);
};

DnsConfigServicePosix::DnsConfigServicePosix()
    : DnsConfigService(kFilePathHosts) {
  // Allow constructing on one thread and living on another.
  DETACH_FROM_SEQUENCE(sequence_checker_);
}

DnsConfigServicePosix::~DnsConfigServicePosix() {
  config_reader_->Cancel();
}

void DnsConfigServicePosix::RefreshConfig() {
  InvalidateConfig();
  InvalidateHosts();
  ReadConfigNow();
  ReadHostsNow();
}

void DnsConfigServicePosix::ReadConfigNow() {
  config_reader_->WorkNow();
}

bool DnsConfigServicePosix::StartWatching() {
  CreateReader();
  // TODO(szym): re-start watcher if that makes sense. http://crbug.com/116139
  watcher_.reset(new Watcher(this));
  return watcher_->Watch();
}

void DnsConfigServicePosix::CreateReader() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  DCHECK(!config_reader_);
  config_reader_ = base::MakeRefCounted<ConfigReader>(this);
}

#if !defined(OS_ANDROID)
bool ConvertResStateToDnsConfig(const struct __res_state& res,
                                DnsConfig* dns_config) {
  DCHECK(dns_config);

  if (!(res.options & RES_INIT))
    return false;

  dns_config->nameservers.clear();

#if defined(OS_APPLE) || defined(OS_FREEBSD)
  union res_sockaddr_union addresses[MAXNS];
  int nscount = res_getservers(const_cast<res_state>(&res), addresses, MAXNS);
  DCHECK_GE(nscount, 0);
  DCHECK_LE(nscount, MAXNS);
  for (int i = 0; i < nscount; ++i) {
    IPEndPoint ipe;
    if (!ipe.FromSockAddr(
            reinterpret_cast<const struct sockaddr*>(&addresses[i]),
            sizeof addresses[i])) {
      return false;
    }
    dns_config->nameservers.push_back(ipe);
  }
#elif defined(OS_LINUX) || defined(OS_CHROMEOS)
  static_assert(std::extent<decltype(res.nsaddr_list)>() >= MAXNS &&
                    std::extent<decltype(res._u._ext.nsaddrs)>() >= MAXNS,
                "incompatible libresolv res_state");
  DCHECK_LE(res.nscount, MAXNS);
  // Initially, glibc stores IPv6 in |_ext.nsaddrs| and IPv4 in |nsaddr_list|.
  // In res_send.c:res_nsend, it merges |nsaddr_list| into |nsaddrs|,
  // but we have to combine the two arrays ourselves.
  for (int i = 0; i < res.nscount; ++i) {
    IPEndPoint ipe;
    const struct sockaddr* addr = nullptr;
    size_t addr_len = 0;
    if (res.nsaddr_list[i].sin_family) {  // The indicator used by res_nsend.
      addr = reinterpret_cast<const struct sockaddr*>(&res.nsaddr_list[i]);
      addr_len = sizeof res.nsaddr_list[i];
    } else if (res._u._ext.nsaddrs[i]) {
      addr = reinterpret_cast<const struct sockaddr*>(res._u._ext.nsaddrs[i]);
      addr_len = sizeof *res._u._ext.nsaddrs[i];
    } else {
      return false;
    }
    if (!ipe.FromSockAddr(addr, addr_len))
      return false;
    dns_config->nameservers.push_back(ipe);
  }
#else   // !(defined(OS_LINUX) || defined(OS_CHROMEOS) || defined(OS_APPLE) ||
        // defined(OS_FREEBSD))
  DCHECK_LE(res.nscount, MAXNS);
  for (int i = 0; i < res.nscount; ++i) {
    IPEndPoint ipe;
    if (!ipe.FromSockAddr(
            reinterpret_cast<const struct sockaddr*>(&res.nsaddr_list[i]),
            sizeof res.nsaddr_list[i])) {
      return false;
    }
    dns_config->nameservers.push_back(ipe);
  }
#endif  // defined(OS_APPLE) || defined(OS_FREEBSD)

  dns_config->search.clear();
  for (int i = 0; (i < MAXDNSRCH) && res.dnsrch[i]; ++i) {
    dns_config->search.push_back(std::string(res.dnsrch[i]));
  }

  dns_config->ndots = res.ndots;
  dns_config->fallback_period = base::TimeDelta::FromSeconds(res.retrans);
  dns_config->attempts = res.retry;
#if defined(RES_ROTATE)
  dns_config->rotate = res.options & RES_ROTATE;
#endif
#if !defined(RES_USE_DNSSEC)
  // Some versions of libresolv don't have support for the DO bit. In this
  // case, we proceed without it.
  static const int RES_USE_DNSSEC = 0;
#endif

  // The current implementation assumes these options are set. They normally
  // cannot be overwritten by /etc/resolv.conf
  const unsigned kRequiredOptions = RES_RECURSE | RES_DEFNAMES | RES_DNSRCH;
  if ((res.options & kRequiredOptions) != kRequiredOptions) {
    dns_config->unhandled_options = true;
    return true;
  }

  const unsigned kUnhandledOptions = RES_USEVC | RES_IGNTC | RES_USE_DNSSEC;
  if (res.options & kUnhandledOptions) {
    dns_config->unhandled_options = true;
    return true;
  }

  if (dns_config->nameservers.empty())
    return false;

  // If any name server is 0.0.0.0, assume the configuration is invalid.
  // TODO(szym): Measure how often this happens. http://crbug.com/125599
  for (unsigned i = 0; i < dns_config->nameservers.size(); ++i) {
    if (dns_config->nameservers[i].address().IsZero())
      return false;
  }
  return true;
}

#endif  // !defined(OS_ANDROID)

}  // namespace internal

// static
std::unique_ptr<DnsConfigService> DnsConfigService::CreateSystemService() {
  // DnsConfigService on iOS doesn't watch the config so its result can become
  // inaccurate at any time.  Disable it to prevent promulgation of inaccurate
  // DnsConfigs.
#ifdef OS_IOS
  return nullptr;
#else   // defined(OS_IOS)
  return std::unique_ptr<DnsConfigService>(
      new internal::DnsConfigServicePosix());
#endif  // defined(OS_IOS)
}

}  // namespace net
