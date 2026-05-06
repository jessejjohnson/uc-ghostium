// Copyright 2026 The Ghostium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ghostium_src/overlay/third_party/blink/renderer/modules/ghostium_fp/canvas2d_noise.h"

#include <array>
#include <cstdint>
#include <vector>

#include "testing/gtest/include/gtest/gtest.h"

namespace blink::ghostium_fp {
namespace {

std::vector<uint8_t> MakeRgba(size_t pixel_count, uint8_t fill) {
  return std::vector<uint8_t>(pixel_count * 4, fill);
}

TEST(Canvas2DNoiseTest, AlphaChannelUntouched) {
  auto pixels = MakeRgba(64, 200);
  // Set alpha to a recognizable distinct value so we can assert it survives.
  for (size_t i = 0; i < pixels.size(); i += 4) {
    pixels[i + 3] = 0x77;
  }
  ApplyCanvas2DPixelNoise(pixels, /*seed=*/0xdeadbeefu);
  for (size_t i = 0; i < pixels.size(); i += 4) {
    EXPECT_EQ(pixels[i + 3], 0x77u) << "alpha at pixel " << i / 4;
  }
}

TEST(Canvas2DNoiseTest, OutputBoundedByOneFromInput) {
  auto pixels = MakeRgba(256, 128);
  ApplyCanvas2DPixelNoise(pixels, 12345u);
  for (size_t i = 0; i < pixels.size(); i += 4) {
    for (size_t c = 0; c < 3; ++c) {
      const int v = pixels[i + c];
      EXPECT_GE(v, 127);
      EXPECT_LE(v, 129);
    }
  }
}

TEST(Canvas2DNoiseTest, SaturatesAtZeroAndMax) {
  auto zero = MakeRgba(16, 0);
  ApplyCanvas2DPixelNoise(zero, 1u);
  for (uint8_t v : zero) {
    EXPECT_LE(v, 1u);  // either 0 (delta=-1 saturated) or 1
  }

  auto max = MakeRgba(16, 255);
  ApplyCanvas2DPixelNoise(max, 1u);
  for (size_t i = 0; i < max.size(); i += 4) {
    for (size_t c = 0; c < 3; ++c) {
      EXPECT_GE(max[i + c], 254u);  // either 255 (delta=+1 saturated) or 254
    }
  }
}

TEST(Canvas2DNoiseTest, DeterministicForSameSeed) {
  auto a = MakeRgba(128, 100);
  auto b = a;
  ApplyCanvas2DPixelNoise(a, 7u);
  ApplyCanvas2DPixelNoise(b, 7u);
  EXPECT_EQ(a, b);
}

TEST(Canvas2DNoiseTest, DifferentSeedsProduceDifferentOutput) {
  auto a = MakeRgba(128, 100);
  auto b = a;
  ApplyCanvas2DPixelNoise(a, 7u);
  ApplyCanvas2DPixelNoise(b, 8u);
  EXPECT_NE(a, b);
}

TEST(Canvas2DNoiseTest, IgnoresTrailingPartialPixel) {
  // 13 bytes = 3 full pixels (12 bytes) + 1 trailing byte.
  std::vector<uint8_t> pixels = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0xab};
  ApplyCanvas2DPixelNoise(pixels, 5u);
  EXPECT_EQ(pixels[12], 0xabu);
}

TEST(Canvas2DNoiseTest, EmptyBufferIsNoOp) {
  std::vector<uint8_t> empty;
  ApplyCanvas2DPixelNoise(empty, 999u);
  EXPECT_TRUE(empty.empty());
}

}  // namespace
}  // namespace blink::ghostium_fp
