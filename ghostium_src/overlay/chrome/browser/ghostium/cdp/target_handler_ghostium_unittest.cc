// Copyright 2026 The Ghostium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ghostium_src/overlay/chrome/browser/ghostium/cdp/target_handler_ghostium.h"

#include <string>

#include "base/json/json_reader.h"
#include "base/values.h"
#include "chrome/test/base/testing_profile.h"
#include "content/public/test/browser_task_environment.h"
#include "ghostium_src/overlay/chrome/browser/ghostium/fingerprint_profile_registry.h"
#include "ghostium_src/overlay/chrome/browser/ghostium/fingerprint_profile_registry_factory.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace ghostium::cdp {
namespace {

base::Value::Dict ParseJson(const std::string& json) {
  auto parsed = base::JSONReader::Read(json);
  CHECK(parsed && parsed->is_dict());
  return std::move(*parsed).TakeDict();
}

class TargetHandlerGhostiumTest : public ::testing::Test {
 protected:
  content::BrowserTaskEnvironment task_env_;
  TestingProfile profile_;
};

TEST_F(TargetHandlerGhostiumTest, CreateRecordsProfileInRegistry) {
  auto dict = ParseJson(R"({"userAgent": "GhostiumUA/1.0"})");
  auto res = ApplyCreateFingerprint(&profile_, dict);
  ASSERT_TRUE(res.ok) << res.error;

  auto* registry = FingerprintProfileRegistryFactory::GetForProfile(&profile_);
  ASSERT_TRUE(registry);
  auto stored = registry->GetProfile(&profile_);
  ASSERT_TRUE(stored.has_value());
  EXPECT_EQ(stored->user_agent, "GhostiumUA/1.0");
}

TEST_F(TargetHandlerGhostiumTest, CreateRejectsMalformedDict) {
  auto dict = ParseJson(R"({"hardwareConcurrency": "twelve"})");
  auto res = ApplyCreateFingerprint(&profile_, dict);
  EXPECT_FALSE(res.ok);
  EXPECT_FALSE(res.error.empty());

  auto* registry = FingerprintProfileRegistryFactory::GetForProfile(&profile_);
  ASSERT_TRUE(registry);
  EXPECT_FALSE(registry->HasProfile(&profile_));
}

TEST_F(TargetHandlerGhostiumTest, CreateRejectsNullContext) {
  auto res = ApplyCreateFingerprint(nullptr, base::Value::Dict());
  EXPECT_FALSE(res.ok);
}

TEST_F(TargetHandlerGhostiumTest, SetReplacesExistingProfile) {
  auto* registry = FingerprintProfileRegistryFactory::GetForProfile(&profile_);
  ASSERT_TRUE(registry);

  // Seed with a prior profile.
  ASSERT_TRUE(
      ApplyCreateFingerprint(&profile_, ParseJson(R"({"userAgent": "v1"})"))
          .ok);

  auto res = HandleSetFingerprint(
      &profile_, ParseJson(R"({"userAgent": "v2"})"));
  ASSERT_TRUE(res.ok) << res.error;
  EXPECT_EQ(registry->GetProfile(&profile_)->user_agent, "v2");
}

TEST_F(TargetHandlerGhostiumTest, SetOnUnseenContextStillSetsProfile) {
  // Spec-C accepts ``set`` on a context that has no prior profile rather
  // than rejecting it; the CDP client may not have used createBrowserContext
  // with the ghostiumFingerprint extension.
  auto res = HandleSetFingerprint(
      &profile_, ParseJson(R"({"userAgent": "fresh"})"));
  ASSERT_TRUE(res.ok) << res.error;
  auto* registry = FingerprintProfileRegistryFactory::GetForProfile(&profile_);
  EXPECT_EQ(registry->GetProfile(&profile_)->user_agent, "fresh");
}

TEST_F(TargetHandlerGhostiumTest, SetRejectsNullContext) {
  auto res = HandleSetFingerprint(nullptr, base::Value::Dict());
  EXPECT_FALSE(res.ok);
}

TEST_F(TargetHandlerGhostiumTest, ClearRemovesRegistration) {
  ASSERT_TRUE(
      ApplyCreateFingerprint(&profile_, ParseJson(R"({"userAgent": "x"})"))
          .ok);
  auto* registry = FingerprintProfileRegistryFactory::GetForProfile(&profile_);
  ASSERT_TRUE(registry->HasProfile(&profile_));

  auto res = HandleClearFingerprint(&profile_);
  ASSERT_TRUE(res.ok);
  EXPECT_FALSE(registry->HasProfile(&profile_));
}

TEST_F(TargetHandlerGhostiumTest, ClearOnUnknownContextSucceeds) {
  // Clearing a never-fingerprinted context is a no-op (idempotent), not an
  // error.
  auto res = HandleClearFingerprint(&profile_);
  EXPECT_TRUE(res.ok);
}

TEST_F(TargetHandlerGhostiumTest, ClearRejectsNullContext) {
  auto res = HandleClearFingerprint(nullptr);
  EXPECT_FALSE(res.ok);
}

}  // namespace
}  // namespace ghostium::cdp
