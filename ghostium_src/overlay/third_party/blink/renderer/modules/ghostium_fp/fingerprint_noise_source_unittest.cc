// Copyright 2026 The Ghostium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ghostium_src/overlay/third_party/blink/renderer/modules/ghostium_fp/fingerprint_noise_source.h"

#include <cstdint>
#include <vector>

#include "testing/gtest/include/gtest/gtest.h"
#include "third_party/blink/renderer/core/frame/local_dom_window.h"
#include "third_party/blink/renderer/core/testing/null_execution_context.h"
#include "third_party/blink/renderer/core/testing/page_test_base.h"
#include "third_party/blink/renderer/platform/testing/task_environment.h"

namespace blink {
namespace {

class FingerprintNoiseSourceTest : public PageTestBase {};

TEST_F(FingerprintNoiseSourceTest, PassthroughWhenNoProfile) {
  auto& source = FingerprintNoiseSource::From(*GetDocument().domWindow());
  EXPECT_FALSE(source.IsActive());

  bool bool_out = true;
  String str_out;
  uint32_t u32_out = 0;
  double d_out = 0;
  Vector<String> langs_out;

  EXPECT_FALSE(source.WebDriverOverride(&bool_out));
  EXPECT_FALSE(source.UserAgentOverride(&str_out));
  EXPECT_FALSE(source.PlatformOverride(&str_out));
  EXPECT_FALSE(source.HardwareConcurrencyOverride(&u32_out));
  EXPECT_FALSE(source.DeviceMemoryOverride(&d_out));
  EXPECT_FALSE(source.LanguagesOverride(&langs_out));
  EXPECT_FALSE(source.ScreenWidthOverride(&u32_out));
  EXPECT_FALSE(source.ScreenHeightOverride(&u32_out));
  EXPECT_FALSE(source.DevicePixelRatioOverride(&d_out));
  EXPECT_FALSE(source.TimezoneOverride(&str_out));
  EXPECT_FALSE(source.WebGLVendorOverride(&str_out));
  EXPECT_FALSE(source.WebGLRendererOverride(&str_out));
  EXPECT_FALSE(source.HasFontWhitelist());
  // No whitelist => every family allowed (passthrough).
  EXPECT_TRUE(source.FontAllowed("Arial"));
  EXPECT_TRUE(source.FontAllowed("serif"));

  WTF::Vector<mojom::blink::GhostiumPluginSpecPtr> plugins_out;
  EXPECT_FALSE(source.PluginsOverride(&plugins_out));
}

TEST_F(FingerprintNoiseSourceTest, OverridesReflectProfile) {
  auto& source = FingerprintNoiseSource::From(*GetDocument().domWindow());

  auto profile = mojom::blink::GhostiumFingerprintProfile::New();
  profile->user_agent = "GhostiumUA/1.0";
  profile->platform = "Linux x86_64";
  profile->hardware_concurrency = 8u;
  profile->device_memory = 4.0;
  profile->webdriver = false;
  profile->device_pixel_ratio = 2.0;
  profile->timezone = "America/Los_Angeles";
  profile->webgl_vendor = "GhostiumGPU";
  profile->webgl_renderer = "GhostiumRenderer";

  auto screen = mojom::blink::GhostiumScreenSpec::New();
  screen->width = 1920;
  screen->height = 1080;
  screen->avail_width = 1920;
  screen->avail_height = 1040;
  screen->color_depth = 24;
  screen->pixel_depth = 24;
  profile->screen = std::move(screen);

  Vector<String> langs;
  langs.push_back("en-US");
  langs.push_back("en");
  profile->languages = std::move(langs);

  Vector<String> fonts;
  fonts.push_back("Roboto");
  profile->fonts_whitelist = std::move(fonts);

  source.SetProfile(std::move(profile));
  EXPECT_TRUE(source.IsActive());

  bool bool_out = true;
  EXPECT_TRUE(source.WebDriverOverride(&bool_out));
  EXPECT_FALSE(bool_out);

  String str_out;
  EXPECT_TRUE(source.UserAgentOverride(&str_out));
  EXPECT_EQ(str_out, "GhostiumUA/1.0");

  EXPECT_TRUE(source.PlatformOverride(&str_out));
  EXPECT_EQ(str_out, "Linux x86_64");

  uint32_t u32_out = 0;
  EXPECT_TRUE(source.HardwareConcurrencyOverride(&u32_out));
  EXPECT_EQ(u32_out, 8u);

  double d_out = 0;
  EXPECT_TRUE(source.DeviceMemoryOverride(&d_out));
  EXPECT_EQ(d_out, 4.0);

  EXPECT_TRUE(source.DevicePixelRatioOverride(&d_out));
  EXPECT_EQ(d_out, 2.0);

  EXPECT_TRUE(source.TimezoneOverride(&str_out));
  EXPECT_EQ(str_out, "America/Los_Angeles");

  EXPECT_TRUE(source.WebGLVendorOverride(&str_out));
  EXPECT_EQ(str_out, "GhostiumGPU");
  EXPECT_TRUE(source.WebGLRendererOverride(&str_out));
  EXPECT_EQ(str_out, "GhostiumRenderer");

  EXPECT_TRUE(source.ScreenWidthOverride(&u32_out));
  EXPECT_EQ(u32_out, 1920u);
  EXPECT_TRUE(source.ScreenHeightOverride(&u32_out));
  EXPECT_EQ(u32_out, 1080u);
  EXPECT_TRUE(source.ScreenAvailHeightOverride(&u32_out));
  EXPECT_EQ(u32_out, 1040u);
  EXPECT_TRUE(source.ScreenColorDepthOverride(&u32_out));
  EXPECT_EQ(u32_out, 24u);

  Vector<String> langs_out;
  EXPECT_TRUE(source.LanguagesOverride(&langs_out));
  ASSERT_EQ(langs_out.size(), 2u);
  EXPECT_EQ(langs_out[0], "en-US");
  EXPECT_EQ(langs_out[1], "en");

  EXPECT_TRUE(source.HasFontWhitelist());
  EXPECT_TRUE(source.FontAllowed("Roboto"));
  EXPECT_TRUE(source.FontAllowed("serif"));
  EXPECT_FALSE(source.FontAllowed("Arial"));
}

TEST_F(FingerprintNoiseSourceTest, ApplyNoiseInactiveIsPassthrough) {
  // Without a profile / seeds, every Apply* path is a no-op (R16).
  auto& source = FingerprintNoiseSource::From(*GetDocument().domWindow());

  uint8_t pixels[16] = {};
  source.ApplyWebGLReadPixelsNoise(base::span(pixels), /*format=*/0x1908,
                                   /*type=*/0x1401);

  float samples[8] = {};
  source.ApplyAudioNoise(base::span(samples), /*sample_index_base=*/0);
  source.ApplyCanvas2DNoise(/*data=*/nullptr);

  for (uint8_t v : pixels) {
    EXPECT_EQ(v, 0u);
  }
  for (float v : samples) {
    EXPECT_EQ(v, 0.0f);
  }
}

TEST_F(FingerprintNoiseSourceTest, Spec_D_NoiseAppliedOnActiveProfile) {
  auto& source = FingerprintNoiseSource::From(*GetDocument().domWindow());
  auto profile = mojom::blink::GhostiumFingerprintProfile::New();
  profile->canvas_seed = 42u;
  profile->webgl_seed = 43u;
  profile->audio_seed = 44u;
  source.SetProfile(std::move(profile));
  ASSERT_TRUE(source.IsActive());

  // Canvas via the byte-buffer entry point.
  std::vector<uint8_t> rgba(64 * 4, 100u);
  source.ApplyCanvas2DPixelNoise(base::span(rgba));
  bool any_canvas_changed = false;
  for (size_t i = 0; i < rgba.size(); i += 4) {
    if (rgba[i] != 100u || rgba[i + 1] != 100u || rgba[i + 2] != 100u) {
      any_canvas_changed = true;
    }
    EXPECT_EQ(rgba[i + 3], 100u);  // alpha untouched
  }
  EXPECT_TRUE(any_canvas_changed);

  // WebGL.
  std::vector<uint8_t> webgl(32 * 4, 50u);
  source.ApplyWebGLReadPixelsNoise(base::span(webgl), /*format=*/0x1908,
                                   /*type=*/0x1401);
  bool any_webgl_changed = false;
  for (size_t i = 0; i < webgl.size(); i += 4) {
    if (webgl[i] != 50u || webgl[i + 1] != 50u || webgl[i + 2] != 50u) {
      any_webgl_changed = true;
      break;
    }
  }
  EXPECT_TRUE(any_webgl_changed);

  // Audio float.
  std::vector<float> audio(128, 0.0f);
  source.ApplyAudioNoise(base::span(audio), /*sample_index_base=*/0);
  bool any_audio_changed = false;
  for (float v : audio) {
    if (v != 0.0f) {
      any_audio_changed = true;
      break;
    }
  }
  EXPECT_TRUE(any_audio_changed);

  // Audio byte.
  std::vector<uint8_t> bytes(64, 128);
  source.ApplyAudioByteNoise(base::span(bytes));
  bool any_byte_changed = false;
  for (uint8_t v : bytes) {
    if (v != 128u) {
      any_byte_changed = true;
      break;
    }
  }
  EXPECT_TRUE(any_byte_changed);
}

TEST_F(FingerprintNoiseSourceTest, Spec_E_PluginsOverridePopulated) {
  auto& source = FingerprintNoiseSource::From(*GetDocument().domWindow());
  auto profile = mojom::blink::GhostiumFingerprintProfile::New();

  WTF::Vector<mojom::blink::GhostiumPluginSpecPtr> plugins;
  auto pdf = mojom::blink::GhostiumPluginSpec::New();
  pdf->name = "PDF Viewer";
  pdf->description = "PDF";
  pdf->filename = "internal-pdf-viewer";
  WTF::Vector<WTF::String> mts;
  mts.push_back("application/pdf");
  mts.push_back("text/pdf");
  pdf->mime_types = std::move(mts);
  plugins.push_back(std::move(pdf));
  profile->plugins = std::move(plugins);

  source.SetProfile(std::move(profile));
  ASSERT_TRUE(source.IsActive());

  WTF::Vector<mojom::blink::GhostiumPluginSpecPtr> out;
  ASSERT_TRUE(source.PluginsOverride(&out));
  ASSERT_EQ(out.size(), 1u);
  EXPECT_EQ(out[0]->name, "PDF Viewer");
  EXPECT_EQ(out[0]->filename, "internal-pdf-viewer");
  ASSERT_EQ(out[0]->mime_types.size(), 2u);
  EXPECT_EQ(out[0]->mime_types[0], "application/pdf");
}

TEST_F(FingerprintNoiseSourceTest, Spec_E_PluginsOverrideEmptyListIsExplicit) {
  // An explicit empty plugin list must report ``true`` (so callers expose an
  // empty navigator.plugins) rather than fall back to upstream.
  auto& source = FingerprintNoiseSource::From(*GetDocument().domWindow());
  auto profile = mojom::blink::GhostiumFingerprintProfile::New();
  profile->plugins = WTF::Vector<mojom::blink::GhostiumPluginSpecPtr>();
  source.SetProfile(std::move(profile));

  WTF::Vector<mojom::blink::GhostiumPluginSpecPtr> out;
  EXPECT_TRUE(source.PluginsOverride(&out));
  EXPECT_TRUE(out.empty());
}

TEST_F(FingerprintNoiseSourceTest, Spec_E_PluginsOverrideAbsentFallsThrough) {
  // A profile that does not set plugins at all must report false so callers
  // pass through to upstream's default plugin list.
  auto& source = FingerprintNoiseSource::From(*GetDocument().domWindow());
  auto profile = mojom::blink::GhostiumFingerprintProfile::New();
  profile->user_agent = "Has-UA-no-Plugins/1.0";
  source.SetProfile(std::move(profile));

  WTF::Vector<mojom::blink::GhostiumPluginSpecPtr> out;
  EXPECT_FALSE(source.PluginsOverride(&out));
}

TEST_F(FingerprintNoiseSourceTest, Spec_E_NavigatorFamilyRoundtrip) {
  // Complete Spec-E surface: every navigator-family override that the
  // patched hooks consult must round-trip through the cached profile.
  auto& source = FingerprintNoiseSource::From(*GetDocument().domWindow());
  auto profile = mojom::blink::GhostiumFingerprintProfile::New();
  profile->user_agent = "GhostiumUA/1.0";
  profile->platform = "Linux x86_64";
  profile->hardware_concurrency = 12u;
  profile->device_memory = 8.0;
  profile->webdriver = false;
  profile->primary_language = "fr-CA";

  WTF::Vector<WTF::String> langs;
  langs.push_back("fr-CA");
  langs.push_back("en-US");
  profile->languages = std::move(langs);

  source.SetProfile(std::move(profile));
  ASSERT_TRUE(source.IsActive());

  bool b = true;
  EXPECT_TRUE(source.WebDriverOverride(&b));
  EXPECT_FALSE(b);

  WTF::String s;
  EXPECT_TRUE(source.UserAgentOverride(&s));
  EXPECT_EQ(s, "GhostiumUA/1.0");
  EXPECT_TRUE(source.PlatformOverride(&s));
  EXPECT_EQ(s, "Linux x86_64");
  EXPECT_TRUE(source.PrimaryLanguageOverride(&s));
  EXPECT_EQ(s, "fr-CA");

  uint32_t hwc = 0;
  EXPECT_TRUE(source.HardwareConcurrencyOverride(&hwc));
  EXPECT_EQ(hwc, 12u);

  double dm = 0;
  EXPECT_TRUE(source.DeviceMemoryOverride(&dm));
  EXPECT_EQ(dm, 8.0);

  WTF::Vector<WTF::String> langs_out;
  ASSERT_TRUE(source.LanguagesOverride(&langs_out));
  ASSERT_EQ(langs_out.size(), 2u);
  EXPECT_EQ(langs_out[0], "fr-CA");
}

TEST_F(FingerprintNoiseSourceTest, Spec_D_NoiseSkippedWhenSeedAbsent) {
  // A profile without any of the noise seeds must still leave buffers
  // untouched even though IsActive() is true.
  auto& source = FingerprintNoiseSource::From(*GetDocument().domWindow());
  auto profile = mojom::blink::GhostiumFingerprintProfile::New();
  profile->user_agent = "GhostiumUA/1.0";
  source.SetProfile(std::move(profile));
  ASSERT_TRUE(source.IsActive());

  std::vector<uint8_t> rgba(16 * 4, 100u);
  source.ApplyCanvas2DPixelNoise(base::span(rgba));
  for (uint8_t v : rgba) {
    EXPECT_EQ(v, 100u);
  }

  std::vector<float> audio(64, 0.0f);
  source.ApplyAudioNoise(base::span(audio), /*sample_index_base=*/0);
  for (float v : audio) {
    EXPECT_EQ(v, 0.0f);
  }
}

}  // namespace
}  // namespace blink
