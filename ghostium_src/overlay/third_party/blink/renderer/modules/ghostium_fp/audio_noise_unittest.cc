// Copyright 2026 The Ghostium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ghostium_src/overlay/third_party/blink/renderer/modules/ghostium_fp/audio_noise.h"

#include <cmath>
#include <cstdint>
#include <vector>

#include "testing/gtest/include/gtest/gtest.h"

namespace blink::ghostium_fp {
namespace {

TEST(AudioNoiseTest, FloatNoiseAmplitudeBounded) {
  std::vector<float> samples(1024, 0.0f);
  ApplyPcmFloatNoise(samples, /*seed=*/0xdeadbeefu,
                      /*sample_index_base=*/0u);
  for (float v : samples) {
    EXPECT_LE(std::abs(v), kPcmNoiseEpsilon * 1.0001f);
    EXPECT_GT(std::abs(v), 0.0f);  // every sample is perturbed
  }
}

TEST(AudioNoiseTest, FloatNoiseDeterministic) {
  std::vector<float> a(512, 0.25f);
  std::vector<float> b = a;
  ApplyPcmFloatNoise(a, 7u, 0u);
  ApplyPcmFloatNoise(b, 7u, 0u);
  for (size_t i = 0; i < a.size(); ++i) {
    EXPECT_EQ(a[i], b[i]);
  }
}

TEST(AudioNoiseTest, FloatNoiseBaseIndexAffectsResult) {
  std::vector<float> a(64, 0.0f);
  std::vector<float> b = a;
  ApplyPcmFloatNoise(a, 1u, 0u);
  ApplyPcmFloatNoise(b, 1u, 100u);
  EXPECT_NE(a, b);
}

TEST(AudioNoiseTest, FloatNoiseDifferentSeedsDifferentOutput) {
  std::vector<float> a(2048, 0.0f);
  std::vector<float> b = a;
  ApplyPcmFloatNoise(a, 1u, 0u);
  ApplyPcmFloatNoise(b, 2u, 0u);
  EXPECT_NE(a, b);
}

TEST(AudioNoiseTest, FloatNoiseChunkConsistency) {
  // Calling on a single 256-sample buffer must equal calling on two 128-
  // sample chunks with the right base index. Models AudioBuffer's
  // ``copyFromChannel`` chunked readback path.
  std::vector<float> single(256, 0.0f);
  ApplyPcmFloatNoise(single, 33u, 0u);

  std::vector<float> chunked(256, 0.0f);
  base::span<float> first(chunked.data(), 128);
  base::span<float> second(chunked.data() + 128, 128);
  ApplyPcmFloatNoise(first, 33u, 0u);
  ApplyPcmFloatNoise(second, 33u, 128u);
  for (size_t i = 0; i < single.size(); ++i) {
    EXPECT_EQ(single[i], chunked[i]) << "i=" << i;
  }
}

TEST(AudioNoiseTest, ByteNoiseSaturatesAtBoundaries) {
  std::vector<uint8_t> hi(256, 255);
  std::vector<uint8_t> lo(256, 0);
  ApplyByteAudioNoise(hi, 11u);
  ApplyByteAudioNoise(lo, 11u);
  for (uint8_t v : hi) {
    EXPECT_GE(v, 254u);
  }
  for (uint8_t v : lo) {
    EXPECT_LE(v, 1u);
  }
}

TEST(AudioNoiseTest, ByteNoiseDeterministic) {
  std::vector<uint8_t> a(64, 128);
  std::vector<uint8_t> b = a;
  ApplyByteAudioNoise(a, 5u);
  ApplyByteAudioNoise(b, 5u);
  EXPECT_EQ(a, b);
}

}  // namespace
}  // namespace blink::ghostium_fp
