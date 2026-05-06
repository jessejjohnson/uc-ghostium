// Copyright 2026 The Ghostium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ghostium_src/overlay/chrome/browser/ghostium/cdp/profile_parser.h"

#include <string>

#include "base/json/json_reader.h"
#include "base/values.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace ghostium::cdp {
namespace {

base::Value::Dict ParseJson(const std::string& json) {
  auto parsed = base::JSONReader::Read(json);
  CHECK(parsed && parsed->is_dict()) << "test JSON not an object: " << json;
  return std::move(*parsed).TakeDict();
}

TEST(ProfileParserTest, EmptyDictParsesToDefaultProfile) {
  auto result = ParseProfile(base::Value::Dict());
  ASSERT_TRUE(result.ok) << result.error;
  EXPECT_FALSE(result.profile.user_agent.has_value());
  EXPECT_FALSE(result.profile.canvas_seed.has_value());
  EXPECT_FALSE(result.profile.screen.has_value());
}

TEST(ProfileParserTest, ScalarFields) {
  auto dict = ParseJson(R"({
    "userAgent": "GhostiumUA/1.0",
    "platform": "Linux x86_64",
    "hardwareConcurrency": 8,
    "deviceMemory": 4,
    "webdriver": false,
    "devicePixelRatio": 2.0,
    "timezone": "America/Los_Angeles",
    "primaryLanguage": "en-US",
    "webglVendor": "GhostiumGPU",
    "webglRenderer": "GhostiumRenderer"
  })");

  auto result = ParseProfile(dict);
  ASSERT_TRUE(result.ok) << result.error;

  const auto& p = result.profile;
  EXPECT_EQ(*p.user_agent, "GhostiumUA/1.0");
  EXPECT_EQ(*p.platform, "Linux x86_64");
  EXPECT_EQ(*p.hardware_concurrency, 8u);
  EXPECT_EQ(*p.device_memory, 4.0);
  EXPECT_FALSE(*p.webdriver);
  EXPECT_EQ(*p.device_pixel_ratio, 2.0);
  EXPECT_EQ(*p.timezone, "America/Los_Angeles");
  EXPECT_EQ(*p.primary_language, "en-US");
  EXPECT_EQ(*p.webgl_vendor, "GhostiumGPU");
  EXPECT_EQ(*p.webgl_renderer, "GhostiumRenderer");
}

TEST(ProfileParserTest, Languages) {
  auto dict = ParseJson(R"({
    "languages": ["en-US", "en", "fr-CA"]
  })");
  auto result = ParseProfile(dict);
  ASSERT_TRUE(result.ok) << result.error;
  ASSERT_TRUE(result.profile.languages.has_value());
  ASSERT_EQ(result.profile.languages->size(), 3u);
  EXPECT_EQ((*result.profile.languages)[0], "en-US");
  EXPECT_EQ((*result.profile.languages)[2], "fr-CA");
}

TEST(ProfileParserTest, ScreenSpec) {
  auto dict = ParseJson(R"({
    "screen": {
      "width": 1920,
      "height": 1080,
      "availWidth": 1920,
      "availHeight": 1040,
      "colorDepth": 24,
      "pixelDepth": 24
    }
  })");
  auto result = ParseProfile(dict);
  ASSERT_TRUE(result.ok) << result.error;
  ASSERT_TRUE(result.profile.screen.has_value());
  EXPECT_EQ(result.profile.screen->width, 1920u);
  EXPECT_EQ(result.profile.screen->avail_height, 1040u);
  EXPECT_EQ(result.profile.screen->color_depth, 24u);
}

TEST(ProfileParserTest, SeedAsInteger) {
  auto dict = ParseJson(R"({"canvasSeed": 42})");
  auto result = ParseProfile(dict);
  ASSERT_TRUE(result.ok) << result.error;
  EXPECT_EQ(*result.profile.canvas_seed, 42u);
}

TEST(ProfileParserTest, SeedAsHexString) {
  auto dict = ParseJson(R"({"webglSeed": "0xdeadbeefcafef00d"})");
  auto result = ParseProfile(dict);
  ASSERT_TRUE(result.ok) << result.error;
  EXPECT_EQ(*result.profile.webgl_seed, 0xdeadbeefcafef00dull);
}

TEST(ProfileParserTest, SeedAsObjectInt) {
  auto dict = ParseJson(R"({"audioSeed": {"int": 7}})");
  auto result = ParseProfile(dict);
  ASSERT_TRUE(result.ok) << result.error;
  EXPECT_EQ(*result.profile.audio_seed, 7u);
}

TEST(ProfileParserTest, SeedAsObjectHex) {
  auto dict = ParseJson(R"({"canvasSeed": {"hex": "0xff"}})");
  auto result = ParseProfile(dict);
  ASSERT_TRUE(result.ok) << result.error;
  EXPECT_EQ(*result.profile.canvas_seed, 255u);
}

TEST(ProfileParserTest, SeedRejectsMissingHexPrefix) {
  auto dict = ParseJson(R"({"canvasSeed": "deadbeef"})");
  auto result = ParseProfile(dict);
  EXPECT_FALSE(result.ok);
  EXPECT_NE(result.error.find("0x"), std::string::npos);
}

TEST(ProfileParserTest, SeedRejectsNegativeInt) {
  auto dict = ParseJson(R"({"canvasSeed": -1})");
  auto result = ParseProfile(dict);
  EXPECT_FALSE(result.ok);
  EXPECT_NE(result.error.find("negative"), std::string::npos);
}

TEST(ProfileParserTest, UserAgentMetadata) {
  auto dict = ParseJson(R"({
    "userAgentMetadata": {
      "brands": [
        {"brand": "Ghostium", "version": "1"},
        {"brand": "Chromium", "version": "135"}
      ],
      "fullVersionList": [
        {"brand": "Ghostium", "version": "1.0.0.1"}
      ],
      "mobile": false,
      "platform": "Linux",
      "platformVersion": "6.5.0",
      "architecture": "x86",
      "bitness": "64",
      "model": "",
      "fullVersion": "135.0.7049.95",
      "wow64": false
    }
  })");
  auto result = ParseProfile(dict);
  ASSERT_TRUE(result.ok) << result.error;
  const auto& p = result.profile;
  ASSERT_TRUE(p.ua_brands.has_value());
  ASSERT_EQ(p.ua_brands->size(), 2u);
  EXPECT_EQ((*p.ua_brands)[1].brand, "Chromium");
  EXPECT_EQ((*p.ua_brands)[1].version, "135");
  ASSERT_TRUE(p.ua_full_version_list.has_value());
  EXPECT_EQ(*p.ua_mobile, false);
  EXPECT_EQ(*p.ua_platform, "Linux");
  EXPECT_EQ(*p.ua_architecture, "x86");
  EXPECT_EQ(*p.ua_bitness, "64");
}

TEST(ProfileParserTest, FontsWhitelist) {
  auto dict = ParseJson(R"({
    "fontsWhitelist": ["Roboto", "Inter"]
  })");
  auto result = ParseProfile(dict);
  ASSERT_TRUE(result.ok) << result.error;
  ASSERT_TRUE(result.profile.fonts_whitelist.has_value());
  EXPECT_EQ(result.profile.fonts_whitelist->size(), 2u);
}

TEST(ProfileParserTest, Plugins) {
  auto dict = ParseJson(R"({
    "plugins": [
      {
        "name": "PDF Viewer",
        "description": "PDF",
        "filename": "internal-pdf-viewer",
        "mimeTypes": ["application/pdf", "text/pdf"]
      }
    ]
  })");
  auto result = ParseProfile(dict);
  ASSERT_TRUE(result.ok) << result.error;
  ASSERT_TRUE(result.profile.plugins.has_value());
  ASSERT_EQ(result.profile.plugins->size(), 1u);
  EXPECT_EQ((*result.profile.plugins)[0].mime_types.size(), 2u);
}

TEST(ProfileParserTest, MediaDevicesValidatesKind) {
  auto dict = ParseJson(R"({
    "mediaDevices": [
      {"deviceId": "x", "kind": "audioinput", "label": "Mic", "groupId": "g1"}
    ]
  })");
  auto result = ParseProfile(dict);
  ASSERT_TRUE(result.ok) << result.error;

  dict = ParseJson(R"({
    "mediaDevices": [
      {"kind": "speaker", "label": "Bad"}
    ]
  })");
  result = ParseProfile(dict);
  EXPECT_FALSE(result.ok);
  EXPECT_NE(result.error.find("kind"), std::string::npos);
}

TEST(ProfileParserTest, WebRTCPolicy) {
  for (const auto& [s, expected] :
       std::initializer_list<
           std::pair<std::string, FingerprintProfile::WebRTCPolicy>>{
           {"default", FingerprintProfile::WebRTCPolicy::kDefault},
           {"default_public_interface_only",
            FingerprintProfile::WebRTCPolicy::kDefaultPublicInterfaceOnly},
           {"disable_non_proxied_udp",
            FingerprintProfile::WebRTCPolicy::kDisableNonProxiedUdp},
           {"disable_both",
            FingerprintProfile::WebRTCPolicy::kDisableBoth},
       }) {
    auto dict = ParseJson("{\"webrtcPolicy\":\"" + s + "\"}");
    auto result = ParseProfile(dict);
    ASSERT_TRUE(result.ok) << result.error;
    EXPECT_EQ(*result.profile.webrtc_policy, expected);
  }

  auto dict = ParseJson(R"({"webrtcPolicy": "bogus"})");
  auto result = ParseProfile(dict);
  EXPECT_FALSE(result.ok);
}

TEST(ProfileParserTest, RejectsWrongTypes) {
  auto dict = ParseJson(R"({"userAgent": 42})");
  auto result = ParseProfile(dict);
  EXPECT_FALSE(result.ok);

  dict = ParseJson(R"({"hardwareConcurrency": "eight"})");
  result = ParseProfile(dict);
  EXPECT_FALSE(result.ok);

  dict = ParseJson(R"({"hardwareConcurrency": -1})");
  result = ParseProfile(dict);
  EXPECT_FALSE(result.ok);

  dict = ParseJson(R"({"languages": [1, 2]})");
  result = ParseProfile(dict);
  EXPECT_FALSE(result.ok);
}

}  // namespace
}  // namespace ghostium::cdp
