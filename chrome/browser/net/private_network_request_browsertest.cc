// Copyright 2020 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <map>
#include <string>

#include "base/strings/string_piece.h"
#include "base/test/metrics/histogram_tester.h"
#include "base/test/scoped_feature_list.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/test/base/in_process_browser_test.h"
#include "content/public/browser/web_contents.h"
#include "content/public/common/content_features.h"
#include "content/public/test/browser_test.h"
#include "content/public/test/browser_test_utils.h"
#include "content/public/test/content_browser_test_utils.h"
#include "net/dns/mock_host_resolver.h"
#include "net/test/embedded_test_server/embedded_test_server.h"
#include "testing/gmock/include/gmock/gmock.h"
#include "third_party/blink/public/mojom/web_feature/web_feature.mojom.h"

namespace {

using blink::mojom::WebFeature;
using testing::ElementsAre;
using testing::IsEmpty;
using testing::Pair;

// We use a custom page that explicitly disables its own favicon (by providing
// an invalid data: URL for it) so as to prevent the browser from making an
// automatic request to /favicon.ico. This is because the automatic request
// messes with our tests, in which we want to trigger a single request from the
// web page to a resource of our choice and observe the side-effect in metrics.
constexpr char kNoFaviconPath[] = "/no-favicon.html";

// Same as kNoFaviconPath, except it carries a header that makes the browser
// consider it came from the `public` address space, irrespective of the fact
// that we loaded the web page from localhost.
constexpr char kTreatAsPublicAddressPath[] =
    "/no-favicon-treat-as-public-address.html";

GURL SecureURL(const net::EmbeddedTestServer& server, const std::string& path) {
  // Test HTTPS servers cannot lie about their hostname, so they yield URLs
  // starting with https://localhost. http://localhost is already a secure
  // context, so we do not bother instantiating an HTTPS server.
  return server.GetURL(path);
}

GURL NonSecureURL(const net::EmbeddedTestServer& server,
                  const std::string& path) {
  return server.GetURL("foo.test", path);
}

GURL LocalSecureURL(const net::EmbeddedTestServer& server) {
  return SecureURL(server, kNoFaviconPath);
}

GURL LocalNonSecureURL(const net::EmbeddedTestServer& server) {
  return NonSecureURL(server, kNoFaviconPath);
}

GURL PublicSecureURL(const net::EmbeddedTestServer& server) {
  return SecureURL(server, kTreatAsPublicAddressPath);
}

GURL PublicNonSecureURL(const net::EmbeddedTestServer& server) {
  return NonSecureURL(server, kTreatAsPublicAddressPath);
}

constexpr WebFeature kAllAddressSpaceFeatures[] = {
    WebFeature::kAddressSpacePrivateSecureContextEmbeddedLocal,
    WebFeature::kAddressSpacePrivateNonSecureContextEmbeddedLocal,
    WebFeature::kAddressSpacePublicSecureContextEmbeddedLocal,
    WebFeature::kAddressSpacePublicNonSecureContextEmbeddedLocal,
    WebFeature::kAddressSpaceUnknownSecureContextEmbeddedLocal,
    WebFeature::kAddressSpaceUnknownNonSecureContextEmbeddedLocal,
    WebFeature::kAddressSpacePublicSecureContextEmbeddedPrivate,
    WebFeature::kAddressSpacePublicNonSecureContextEmbeddedPrivate,
    WebFeature::kAddressSpaceUnknownSecureContextEmbeddedPrivate,
    WebFeature::kAddressSpaceUnknownNonSecureContextEmbeddedPrivate,
    WebFeature::kAddressSpacePrivateSecureContextNavigatedToLocal,
    WebFeature::kAddressSpacePrivateNonSecureContextNavigatedToLocal,
    WebFeature::kAddressSpacePublicSecureContextNavigatedToLocal,
    WebFeature::kAddressSpacePublicNonSecureContextNavigatedToLocal,
    WebFeature::kAddressSpaceUnknownSecureContextNavigatedToLocal,
    WebFeature::kAddressSpaceUnknownNonSecureContextNavigatedToLocal,
    WebFeature::kAddressSpacePublicSecureContextNavigatedToPrivate,
    WebFeature::kAddressSpacePublicNonSecureContextNavigatedToPrivate,
    WebFeature::kAddressSpaceUnknownSecureContextNavigatedToPrivate,
    WebFeature::kAddressSpaceUnknownNonSecureContextNavigatedToPrivate,
};

// Returns a map of WebFeature to bucket count. Skips buckets with zero counts.
std::map<WebFeature, int> GetAddressSpaceFeatureBucketCounts(
    const base::HistogramTester& tester) {
  std::map<WebFeature, int> counts;
  for (WebFeature feature : kAllAddressSpaceFeatures) {
    int count = tester.GetBucketCount("Blink.UseCounter.Features", feature);
    if (count == 0) {
      continue;
    }

    counts.emplace(feature, count);
  }
  return counts;
}

// CORS-RFC1918 is a web platform specification aimed at securing requests made
// from public websites to the private network and localhost. It is entirely
// implemented in content/. Its integration with Blink UseCounters cannot be
// tested in content/, however, thus we define this standalone test here.
//
// See also:
//
//  - specification: https://wicg.github.io/cors-rfc1918.
//  - feature browsertests in content/: RenderFrameHostImplTest.
//
class PrivateNetworkRequestBrowserTest : public InProcessBrowserTest {
 public:
  content::WebContents* web_contents() {
    return browser()->tab_strip_model()->GetActiveWebContents();
  }

  bool NavigateAndFlushHistograms() {
    // Commit a new navigation in order to flush UseCounters incremented during
    // the last navigation to the browser process, so they are reflected in
    // histograms.
    return content::NavigateToURL(web_contents(), GURL("about:blank"));
  }

  // Never returns nullptr. The returned server is already Start()ed.
  //
  // NOTE: This is defined as a method on the test fixture instead of a free
  // function because GetChromeTestDataDir() is a test fixture method itself.
  // We return a unique_ptr because EmbeddedTestServer is not movable and C++17
  // support is not available at time of writing.
  std::unique_ptr<net::EmbeddedTestServer> NewServer() {
    auto server = std::make_unique<net::EmbeddedTestServer>();
    server->AddDefaultHandlers(GetChromeTestDataDir());
    EXPECT_TRUE(server->Start());
    return server;
  }

 private:
  void SetUpOnMainThread() final { host_resolver()->AddRule("*", "127.0.0.1"); }
};

// This test verifies that no feature is counted for the initial navigation from
// a new tab to a page served by localhost.
//
// Regression test for https://crbug.com/1134601.
IN_PROC_BROWSER_TEST_F(PrivateNetworkRequestBrowserTest,
                       DoesNotRecordAddressSpaceFeatureForInitialNavigation) {
  base::HistogramTester histogram_tester;
  std::unique_ptr<net::EmbeddedTestServer> server = NewServer();

  EXPECT_TRUE(content::NavigateToURL(web_contents(), PublicSecureURL(*server)));
  EXPECT_TRUE(NavigateAndFlushHistograms());

  EXPECT_THAT(GetAddressSpaceFeatureBucketCounts(histogram_tester), IsEmpty());
}

// This test verifies that no feature is counted for top-level navigations from
// a public page to a local page.
//
// TODO(crbug.com/1129326): Revisit this once the story around top-level
// navigations is closer to being resolved. Counting these events will help
// decide what to do.
IN_PROC_BROWSER_TEST_F(PrivateNetworkRequestBrowserTest,
                       DoesNotRecordAddressSpaceFeatureForRegularNavigation) {
  base::HistogramTester histogram_tester;
  std::unique_ptr<net::EmbeddedTestServer> server = NewServer();

  EXPECT_TRUE(content::NavigateToURL(web_contents(), PublicSecureURL(*server)));
  EXPECT_TRUE(content::NavigateToURL(web_contents(), LocalSecureURL(*server)));
  EXPECT_TRUE(NavigateAndFlushHistograms());

  EXPECT_THAT(GetAddressSpaceFeatureBucketCounts(histogram_tester), IsEmpty());
}

// This test verifies that when a secure context served from the public address
// space loads a resource from the local network, the correct WebFeature is
// use-counted.
// Disabled, as explained in https://crbug.com/1143206
IN_PROC_BROWSER_TEST_F(PrivateNetworkRequestBrowserTest,
                       DISABLED_RecordsAddressSpaceFeatureForFetch) {
  base::HistogramTester histogram_tester;
  std::unique_ptr<net::EmbeddedTestServer> server = NewServer();

  EXPECT_TRUE(content::NavigateToURL(web_contents(), PublicSecureURL(*server)));
  EXPECT_EQ(true, content::EvalJs(web_contents(), R"(
    fetch("defaultresponse").then(response => response.ok)
  )"));
  EXPECT_TRUE(NavigateAndFlushHistograms());

  EXPECT_THAT(
      GetAddressSpaceFeatureBucketCounts(histogram_tester),
      ElementsAre(
          Pair(WebFeature::kAddressSpacePublicSecureContextEmbeddedLocal, 1)));
}

// This test verifies that when a non-secure context served from the public
// address space loads a resource from the local network, the correct WebFeature
// is use-counted.
IN_PROC_BROWSER_TEST_F(
    PrivateNetworkRequestBrowserTest,
    DISABLED_RecordsAddressSpaceFeatureForFetchInNonSecureContext) {
  base::HistogramTester histogram_tester;
  std::unique_ptr<net::EmbeddedTestServer> server = NewServer();

  EXPECT_TRUE(
      content::NavigateToURL(web_contents(), PublicNonSecureURL(*server)));
  EXPECT_EQ(true, content::EvalJs(web_contents(), R"(
    fetch("defaultresponse").then(response => response.ok)
  )"));
  EXPECT_TRUE(NavigateAndFlushHistograms());

  EXPECT_THAT(
      GetAddressSpaceFeatureBucketCounts(histogram_tester),
      ElementsAre(Pair(
          WebFeature::kAddressSpacePublicNonSecureContextEmbeddedLocal, 1)));
}

// This test verifies that when page embeds an empty iframe pointing to
// about:blank, no address space feature is recorded. It serves as a basis for
// comparison with the following tests, which test behavior with iframes.
IN_PROC_BROWSER_TEST_F(
    PrivateNetworkRequestBrowserTest,
    DoesNotRecordAddressSpaceFeatureForAboutBlankNavigation) {
  base::HistogramTester histogram_tester;
  std::unique_ptr<net::EmbeddedTestServer> server = NewServer();

  EXPECT_TRUE(
      content::NavigateToURL(web_contents(), PublicNonSecureURL(*server)));
  EXPECT_TRUE(content::ExecJs(web_contents(), R"(
    new Promise(resolve => {
      const child = document.createElement("iframe");
      child.src = "about:blank";
      child.onload = resolve;
      document.body.appendChild(child);
    })
  )"));
  EXPECT_TRUE(NavigateAndFlushHistograms());

  EXPECT_THAT(GetAddressSpaceFeatureBucketCounts(histogram_tester), IsEmpty());
}

// This test verifies that when a non-secure context served from the public
// address space loads a child frame from the local network, the correct
// WebFeature is use-counted.
IN_PROC_BROWSER_TEST_F(PrivateNetworkRequestBrowserTest,
                       RecordsAddressSpaceFeatureForChildNavigation) {
  base::HistogramTester histogram_tester;
  std::unique_ptr<net::EmbeddedTestServer> server = NewServer();

  EXPECT_TRUE(
      content::NavigateToURL(web_contents(), PublicNonSecureURL(*server)));

  base::StringPiece script_template = R"(
    new Promise(resolve => {
      const child = document.createElement("iframe");
      child.src = $1;
      child.onload = resolve;
      document.body.appendChild(child);
    })
  )";
  EXPECT_TRUE(content::ExecJs(
      web_contents(),
      content::JsReplace(script_template, LocalNonSecureURL(*server))));
  EXPECT_TRUE(NavigateAndFlushHistograms());

  EXPECT_THAT(
      GetAddressSpaceFeatureBucketCounts(histogram_tester),
      ElementsAre(Pair(
          WebFeature::kAddressSpacePublicNonSecureContextNavigatedToLocal, 1)));
}

// This test verifies that when a non-secure context served from the public
// address space loads a grand-child frame from the local network, the correct
// WebFeature is use-counted. If inheritance did not work correctly, the
// intermediate about:blank frame might confuse the address space logic.
IN_PROC_BROWSER_TEST_F(PrivateNetworkRequestBrowserTest,
                       RecordsAddressSpaceFeatureForGrandchildNavigation) {
  base::HistogramTester histogram_tester;
  std::unique_ptr<net::EmbeddedTestServer> server = NewServer();

  EXPECT_TRUE(
      content::NavigateToURL(web_contents(), PublicNonSecureURL(*server)));

  base::StringPiece script_template = R"(
    function addChildFrame(doc, src) {
      return new Promise(resolve => {
        const child = doc.createElement("iframe");
        child.src = src;
        child.onload = () => { resolve(child); };
        doc.body.appendChild(child);
      });
    }

    addChildFrame(document, "about:blank")
      .then(child => addChildFrame(child.contentDocument, $1))
  )";
  EXPECT_TRUE(content::ExecJs(
      web_contents(),
      content::JsReplace(script_template, LocalNonSecureURL(*server))));
  EXPECT_TRUE(NavigateAndFlushHistograms());

  EXPECT_THAT(
      GetAddressSpaceFeatureBucketCounts(histogram_tester),
      ElementsAre(Pair(
          WebFeature::kAddressSpacePublicNonSecureContextNavigatedToLocal, 1)));
}

class PrivateNetworkRequestWithFeatureEnabledBrowserTest
    : public PrivateNetworkRequestBrowserTest {
 public:
  PrivateNetworkRequestWithFeatureEnabledBrowserTest() {
    features_.InitAndEnableFeature(
        features::kBlockInsecurePrivateNetworkRequests);
  }

 private:
  base::test::ScopedFeatureList features_;
};

// This test verifies that private network requests that are blocked do not
// result in a WebFeature being use-counted.
IN_PROC_BROWSER_TEST_F(PrivateNetworkRequestWithFeatureEnabledBrowserTest,
                       DoesNotRecordAddressSpaceFeatureForBlockedRequests) {
  base::HistogramTester histogram_tester;
  std::unique_ptr<net::EmbeddedTestServer> server = NewServer();

  EXPECT_TRUE(
      content::NavigateToURL(web_contents(), PublicNonSecureURL(*server)));
  EXPECT_EQ(true, content::EvalJs(web_contents(), R"(
    fetch("defaultresponse").catch(() => true)
  )"));
  EXPECT_TRUE(NavigateAndFlushHistograms());

  EXPECT_THAT(GetAddressSpaceFeatureBucketCounts(histogram_tester), IsEmpty());
}

}  // namespace
