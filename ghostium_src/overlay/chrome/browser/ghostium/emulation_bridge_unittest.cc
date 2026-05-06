// Copyright 2026 The Ghostium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ghostium_src/overlay/chrome/browser/ghostium/emulation_bridge.h"

#include "chrome/test/base/testing_profile.h"
#include "content/public/browser/web_contents.h"
#include "content/public/test/browser_task_environment.h"
#include "content/public/test/test_web_contents_factory.h"
#include "ghostium_src/overlay/chrome/browser/ghostium/fingerprint_profile.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace ghostium {
namespace {

class EmulationBridgeTest : public ::testing::Test {
 protected:
  content::BrowserTaskEnvironment task_env_;
  TestingProfile profile_;
  content::TestWebContentsFactory factory_;
};

TEST_F(EmulationBridgeTest, ApplyToWebContentsSetsUserAgentOverride) {
  auto* wc = factory_.CreateWebContents(&profile_);

  FingerprintProfile p;
  p.user_agent = "GhostiumUA/1.0";
  EmulationBridge::ApplyToWebContents(wc, p);

  // ``GetUserAgentOverride`` returns the live override registered on the
  // WebContents. After ApplyToWebContents this must reflect the profile's
  // string value.
  EXPECT_EQ(wc->GetUserAgentOverride().ua_string_override, "GhostiumUA/1.0");
}

TEST_F(EmulationBridgeTest, ApplyToWebContentsWithMetadata) {
  auto* wc = factory_.CreateWebContents(&profile_);

  FingerprintProfile p;
  p.user_agent = "GhostiumUA/1.0";
  p.ua_platform = "Linux";
  p.ua_mobile = false;
  EmulationBridge::ApplyToWebContents(wc, p);

  const auto& override = wc->GetUserAgentOverride();
  EXPECT_EQ(override.ua_string_override, "GhostiumUA/1.0");
  ASSERT_TRUE(override.ua_metadata_override.has_value());
  EXPECT_EQ(override.ua_metadata_override->platform, "Linux");
  EXPECT_FALSE(override.ua_metadata_override->mobile);
}

TEST_F(EmulationBridgeTest, ApplyWithoutUaFieldsIsNoop) {
  auto* wc = factory_.CreateWebContents(&profile_);
  FingerprintProfile p;
  // Profile has only canvas-style fields, no UA.
  p.canvas_seed = 42u;
  EmulationBridge::ApplyToWebContents(wc, p);
  EXPECT_TRUE(wc->GetUserAgentOverride().ua_string_override.empty());
  EXPECT_FALSE(wc->GetUserAgentOverride().ua_metadata_override.has_value());
}

TEST_F(EmulationBridgeTest, ClearForWebContentsResetsOverride) {
  auto* wc = factory_.CreateWebContents(&profile_);

  FingerprintProfile p;
  p.user_agent = "GhostiumUA/1.0";
  EmulationBridge::ApplyToWebContents(wc, p);
  ASSERT_EQ(wc->GetUserAgentOverride().ua_string_override, "GhostiumUA/1.0");

  EmulationBridge::ClearForWebContents(wc);
  EXPECT_TRUE(wc->GetUserAgentOverride().ua_string_override.empty());
  EXPECT_FALSE(wc->GetUserAgentOverride().ua_metadata_override.has_value());
}

TEST_F(EmulationBridgeTest, NullPointersAreSafe) {
  EmulationBridge::Apply(nullptr, FingerprintProfile{});
  EmulationBridge::ApplyToWebContents(nullptr, FingerprintProfile{});
  EmulationBridge::Clear(nullptr);
  EmulationBridge::ClearForWebContents(nullptr);
}

}  // namespace
}  // namespace ghostium
