// Copyright 2026 The Ghostium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ghostium_src/overlay/third_party/blink/renderer/modules/ghostium_fp/webgl_noise.h"

#include <cstdint>
#include <cstring>
#include <vector>

#include "testing/gtest/include/gtest/gtest.h"

namespace blink::ghostium_fp {
namespace {

std::vector<uint8_t> MakeRgba(size_t pixel_count, uint8_t fill) {
  return std::vector<uint8_t>(pixel_count * 4, fill);
}

TEST(WebGLNoiseTest, RGBA8Path_AlphaUntouched) {
  auto pixels = MakeRgba(32, 100);
  for (size_t i = 0; i < pixels.size(); i += 4) {
    pixels[i + 3] = 0xee;
  }
  ApplyWebGLReadPixelsRGBA8(pixels, /*seed=*/77u);
  for (size_t i = 0; i < pixels.size(); i += 4) {
    EXPECT_EQ(pixels[i + 3], 0xeeu);
  }
}

TEST(WebGLNoiseTest, RGBA8Path_DeterministicAndBoundedByOne) {
  auto a = MakeRgba(16, 50);
  auto b = a;
  ApplyWebGLReadPixelsRGBA8(a, 9u);
  ApplyWebGLReadPixelsRGBA8(b, 9u);
  EXPECT_EQ(a, b);
  for (size_t i = 0; i < a.size(); i += 4) {
    for (size_t c = 0; c < 3; ++c) {
      EXPECT_GE(static_cast<int>(a[i + c]), 49);
      EXPECT_LE(static_cast<int>(a[i + c]), 51);
    }
  }
}

TEST(WebGLNoiseTest, FloatPath_OneUlpToggle) {
  std::vector<float> samples(16, 1.0f);
  std::vector<float> baseline = samples;
  ApplyWebGLReadPixelsFloat(samples, 42u);
  // Each sample is either unchanged or perturbed by exactly 1 ULP.
  for (size_t i = 0; i < samples.size(); ++i) {
    if (samples[i] == baseline[i]) {
      continue;
    }
    uint32_t before = 0;
    uint32_t after = 0;
    std::memcpy(&before, &baseline[i], sizeof(before));
    std::memcpy(&after, &samples[i], sizeof(after));
    EXPECT_EQ(before ^ after, 1u);
  }
}

TEST(WebGLNoiseTest, FloatPath_SameSeedSameOutput) {
  std::vector<float> a(64, 0.5f);
  std::vector<float> b = a;
  ApplyWebGLReadPixelsFloat(a, 1234u);
  ApplyWebGLReadPixelsFloat(b, 1234u);
  for (size_t i = 0; i < a.size(); ++i) {
    EXPECT_EQ(a[i], b[i]);
  }
}

TEST(WebGLNoiseTest, FloatPath_DifferentSeedsDifferentOutput) {
  std::vector<float> a(256, 0.5f);
  std::vector<float> b = a;
  ApplyWebGLReadPixelsFloat(a, 1u);
  ApplyWebGLReadPixelsFloat(b, 2u);
  EXPECT_NE(a, b);
}

TEST(WebGLNoiseTest, DispatcherRoutesByFormat) {
  // RGBA / UNSIGNED_BYTE => byte path.
  auto rgba = MakeRgba(16, 100);
  ApplyWebGLReadPixelsNoise(rgba, kGlRgba, kGlUnsignedByte, 5u);
  bool any_changed = false;
  for (size_t i = 0; i < rgba.size(); i += 4) {
    if (rgba[i] != 100u || rgba[i + 1] != 100u || rgba[i + 2] != 100u) {
      any_changed = true;
      break;
    }
  }
  EXPECT_TRUE(any_changed);

  // GL_FLOAT => float path. Reuse the same byte buffer reinterpreted as
  // floats.
  std::vector<float> floats(64, 1.0f);
  base::span<uint8_t> bytes(reinterpret_cast<uint8_t*>(floats.data()),
                             floats.size() * sizeof(float));
  ApplyWebGLReadPixelsNoise(bytes, kGlRgba, kGlFloat, 5u);
  bool any_float_changed = false;
  for (float v : floats) {
    if (v != 1.0f) {
      any_float_changed = true;
      break;
    }
  }
  EXPECT_TRUE(any_float_changed);
}

TEST(WebGLNoiseTest, DispatcherUnknownFormatIsNoOp) {
  auto rgba = MakeRgba(8, 88);
  // 0x1402 == GL_SHORT (a format we don't recognise).
  ApplyWebGLReadPixelsNoise(rgba, kGlRgba, /*type=*/0x1402u, 5u);
  for (uint8_t v : rgba) {
    EXPECT_EQ(v, 88u);
  }
}

}  // namespace
}  // namespace blink::ghostium_fp
