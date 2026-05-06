// Copyright 2026 The Ghostium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ghostium_src/overlay/third_party/blink/renderer/modules/ghostium_fp/audio_noise.h"

#include <algorithm>

#include "ghostium_src/overlay/third_party/blink/renderer/modules/ghostium_fp/mulberry32.h"

namespace blink::ghostium_fp {

void ApplyPcmFloatNoise(base::span<float> samples, uint32_t seed,
                         uint32_t sample_index_base) {
  for (size_t i = 0; i < samples.size(); ++i) {
    Mulberry32 rng(seed ^ (sample_index_base + static_cast<uint32_t>(i)));
    const float delta = (rng.Next() & 1u) ? kPcmNoiseEpsilon
                                           : -kPcmNoiseEpsilon;
    samples[i] += delta;
  }
}

void ApplyByteAudioNoise(base::span<uint8_t> samples, uint32_t seed) {
  for (size_t i = 0; i < samples.size(); ++i) {
    Mulberry32 rng(seed ^ static_cast<uint32_t>(i));
    const int delta = static_cast<int>(rng.Next() & 1u) * 2 - 1;
    const int v = static_cast<int>(samples[i]) + delta;
    samples[i] = static_cast<uint8_t>(std::clamp(v, 0, 255));
  }
}

}  // namespace blink::ghostium_fp
