// Copyright 2026 The Ghostium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GHOSTIUM_OVERLAY_THIRD_PARTY_BLINK_RENDERER_MODULES_GHOSTIUM_FP_WEBGL_NOISE_H_
#define GHOSTIUM_OVERLAY_THIRD_PARTY_BLINK_RENDERER_MODULES_GHOSTIUM_FP_WEBGL_NOISE_H_

#include <cstdint>

#include "base/containers/span.h"

namespace blink::ghostium_fp {

// Subset of GL pixel format / type constants that we need to recognise. The
// numeric values are stable across GLES2 / GL 2.0+ headers; we keep them
// inline here so that the noise functions are unit-testable without pulling
// in ``//gpu/GLES2/gl2.h``.
inline constexpr uint32_t kGlRgb = 0x1907u;
inline constexpr uint32_t kGlRgba = 0x1908u;
inline constexpr uint32_t kGlUnsignedByte = 0x1401u;
inline constexpr uint32_t kGlFloat = 0x1406u;
inline constexpr uint32_t kGlHalfFloat = 0x140Bu;

// Apply deterministic ±1 LSB noise to RGBA8 ``readPixels`` output. Same
// algorithm as ``ApplyCanvas2DPixelNoise`` but seeded by ``webgl_seed``.
// The buffer must be a packed sequence of 4-byte pixels; trailing bytes
// past the last full pixel are left untouched.
void ApplyWebGLReadPixelsRGBA8(base::span<uint8_t> pixels, uint32_t seed);

// Apply a deterministic single-mantissa-bit toggle to a ``readPixels``
// buffer of ``GL_FLOAT`` values. The noise is keyed per scalar
// (``Mulberry32(seed ^ scalar_index)``), so a 4-channel RGBA32F buffer of N
// pixels has 4N independent perturbations. Magnitude is ±1 ULP - well
// below the noise floor of any GPU readback path but enough to randomize
// fingerprint hashes that rely on exact float bit patterns.
void ApplyWebGLReadPixelsFloat(base::span<float> pixels, uint32_t seed);

// Format-aware dispatcher. Recognises the two formats the spec mandates
// (RGBA8 + GL_FLOAT). Other formats are passed through unchanged: the
// safety contract is "never corrupt rendered content", not "noise every
// possible format".
void ApplyWebGLReadPixelsNoise(base::span<uint8_t> pixels,
                                uint32_t format,
                                uint32_t type,
                                uint32_t seed);

}  // namespace blink::ghostium_fp

#endif  // GHOSTIUM_OVERLAY_THIRD_PARTY_BLINK_RENDERER_MODULES_GHOSTIUM_FP_WEBGL_NOISE_H_
