// Copyright 2020 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef COMPONENTS_PASSWORD_MANAGER_CORE_BROWSER_SITE_AFFILIATION_AFFILIATION_SERVICE_IMPL_H_
#define COMPONENTS_PASSWORD_MANAGER_CORE_BROWSER_SITE_AFFILIATION_AFFILIATION_SERVICE_IMPL_H_

#include <map>
#include <memory>
#include <vector>

#include "components/password_manager/core/browser/site_affiliation/affiliation_service.h"

#include "base/memory/scoped_refptr.h"
#include "components/password_manager/core/browser/android_affiliation/affiliation_fetcher_delegate.h"

namespace network {
class SharedURLLoaderFactory;
}

namespace syncer {
class SyncService;
}

namespace password_manager {

class AffiliationFetcherInterface;

class AffiliationServiceImpl : public AffiliationService,
                               public AffiliationFetcherDelegate {
 public:
  explicit AffiliationServiceImpl(
      syncer::SyncService* sync_service,
      scoped_refptr<network::SharedURLLoaderFactory> url_loader_factory);
  ~AffiliationServiceImpl() override;

  // Prefetches change password URLs and saves them to |change_password_urls_|
  // map. The verification if affiliation based matching is enabled must be
  // performed.
  void PrefetchChangePasswordURLs(
      const std::vector<url::SchemeHostPort>& tuple_origins) override;

  // Clears the |change_password_urls_| map and cancels prefetch if still
  // running.
  void Clear() override;

  // In case no valid URL was found, a method returns an empty URL.
  GURL GetChangePasswordURL(
      const url::SchemeHostPort& scheme_host_port) const override;

  AffiliationFetcherInterface* GetFetcherForTesting() { return fetcher_.get(); }

 private:
  // AffiliationFetcherDelegate:
  void OnFetchSucceeded(
      std::unique_ptr<AffiliationFetcherDelegate::Result> result) override;
  void OnFetchFailed() override;
  void OnMalformedResponse() override;

  // Converts new |tuple_origins| to facets and inserts them to the
  // |change_password_urls_|.
  std::vector<FacetURI> ConvertMissingSchemeHostPortsToFacets(
      const std::vector<url::SchemeHostPort>& tuple_origins);

  // Calls Affiliation Fetcher and starts a request for |facets| affiliations.
  void RequestFacetsAffiliations(const std::vector<FacetURI>& facets);

  syncer::SyncService* sync_service_;
  const scoped_refptr<network::SharedURLLoaderFactory> url_loader_factory_;
  std::vector<url::SchemeHostPort> requested_tuple_origins_;
  std::map<url::SchemeHostPort, GURL> change_password_urls_;
  // TODO(crbug.com/1117045): A vector of pending fetchers to be created.
  std::unique_ptr<AffiliationFetcherInterface> fetcher_;
};

}  // namespace password_manager
#endif  // COMPONENTS_PASSWORD_MANAGER_CORE_BROWSER_SITE_AFFILIATION_AFFILIATION_SERVICE_IMPL_H_
