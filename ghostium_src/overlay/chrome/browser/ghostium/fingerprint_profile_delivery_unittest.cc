// Copyright 2026 The Ghostium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ghostium_src/overlay/chrome/browser/ghostium/fingerprint_profile_delivery.h"

#include <memory>

#include "base/test/task_environment.h"
#include "chrome/test/base/testing_profile.h"
#include "content/public/test/browser_task_environment.h"
#include "content/public/test/test_web_contents_factory.h"
#include "ghostium_src/overlay/chrome/browser/ghostium/fingerprint_profile.h"
#include "ghostium_src/overlay/chrome/browser/ghostium/fingerprint_profile_registry.h"
#include "ghostium_src/overlay/chrome/browser/ghostium/fingerprint_profile_registry_factory.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace ghostium {
namespace {

class FingerprintProfileDeliveryTest : public ::testing::Test {
 protected:
  content::BrowserTaskEnvironment task_env_;
  TestingProfile profile_;
  content::TestWebContentsFactory web_contents_factory_;
};

TEST_F(FingerprintProfileDeliveryTest, ConstructAttachesToRegistry) {
  // Delivery's constructor looks up the registry via the factory; simply
  // building it must not CHECK-fail on any Profile configuration.
  FingerprintProfileDelivery delivery(&profile_);
  // No profile has been set; ObserveWebContents must be safe.
  auto* web_contents = web_contents_factory_.CreateWebContents(&profile_);
  delivery.ObserveWebContents(web_contents);
  // Double-observation must also be safe (idempotent).
  delivery.ObserveWebContents(web_contents);
}

TEST_F(FingerprintProfileDeliveryTest, NoPushWhenNoProfile) {
  // R16 invariant: a BrowserContext with no registered profile must not
  // trigger Mojo binding. We cannot observe the absence directly without
  // mocking AssociatedInterfaceProvider, but we can exercise the hot path
  // and assert it does not crash.
  FingerprintProfileDelivery delivery(&profile_);
  auto* web_contents = web_contents_factory_.CreateWebContents(&profile_);
  delivery.ObserveWebContents(web_contents);
  task_env_.RunUntilIdle();
}

TEST_F(FingerprintProfileDeliveryTest, SetProfileTriggersRepush) {
  // Register the delivery as an observer implicitly via its ctor; then set
  // a profile and verify the observer callback does not crash. The actual
  // push to the renderer is covered by E2E tests once Spec-C lands.
  FingerprintProfileDelivery delivery(&profile_);
  auto* web_contents = web_contents_factory_.CreateWebContents(&profile_);
  delivery.ObserveWebContents(web_contents);

  auto* registry = FingerprintProfileRegistryFactory::GetForProfile(&profile_);
  ASSERT_TRUE(registry);

  FingerprintProfile fp;
  fp.user_agent = "UA-Test";
  registry->SetProfile(&profile_, std::move(fp));
  task_env_.RunUntilIdle();
}

}  // namespace
}  // namespace ghostium
