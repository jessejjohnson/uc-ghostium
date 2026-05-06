// Copyright 2026 The Ghostium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GHOSTIUM_OVERLAY_THIRD_PARTY_BLINK_RENDERER_MODULES_GHOSTIUM_FP_AUDIO_NOISE_H_
#define GHOSTIUM_OVERLAY_THIRD_PARTY_BLINK_RENDERER_MODULES_GHOSTIUM_FP_AUDIO_NOISE_H_

#include <cstdint>

#include "base/containers/span.h"

namespace blink::ghostium_fp {

// Magnitude of the per-sample perturbation applied to PCM float buffers.
// Below human hearing threshold and well below the noise floor of any
// consumer microphone, but sufficient to randomize ``DynamicsCompressor``,
// FFT-based readback (``AnalyserNode.getFloatFrequencyData``), and
// ``AudioBuffer.getChannelData`` fingerprint probes.
inline constexpr float kPcmNoiseEpsilon = 1e-7f;

// Apply ±``kPcmNoiseEpsilon`` perturbation to each sample, keyed by
// ``Mulberry32(seed ^ (sample_index_base + i))``. ``sample_index_base`` is
// the absolute index of the first sample in the span: callers that read
// audio in chunks (e.g. ``copyFromChannel`` with offset) pass the offset
// here so the noise is consistent regardless of chunking.
//
// Determinism: same seed + same input + same base index => same output.
void ApplyPcmFloatNoise(base::span<float> samples, uint32_t seed,
                         uint32_t sample_index_base);

// AnalyserNode-style ±1 LSB noise on a uint8 magnitude / time-domain
// buffer. Used by ``getByteFrequencyData`` and ``getByteTimeDomainData``;
// the algorithm is identical to the Canvas2D channel-byte noise (signed
// ±1 with saturation at 0 and 255), seeded by ``audio_seed``.
void ApplyByteAudioNoise(base::span<uint8_t> samples, uint32_t seed);

}  // namespace blink::ghostium_fp

#endif  // GHOSTIUM_OVERLAY_THIRD_PARTY_BLINK_RENDERER_MODULES_GHOSTIUM_FP_AUDIO_NOISE_H_
