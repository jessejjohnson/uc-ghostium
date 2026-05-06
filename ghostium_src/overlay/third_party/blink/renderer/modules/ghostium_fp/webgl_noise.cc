// Copyright 2026 The Ghostium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ghostium_src/overlay/third_party/blink/renderer/modules/ghostium_fp/webgl_noise.h"

#include <algorithm>
#include <cstring>

#include "ghostium_src/overlay/third_party/blink/renderer/modules/ghostium_fp/canvas2d_noise.h"
#include "ghostium_src/overlay/third_party/blink/renderer/modules/ghostium_fp/mulberry32.h"

namespace blink::ghostium_fp {

void ApplyWebGLReadPixelsRGBA8(base::span<uint8_t> pixels, uint32_t seed) {
  // Identical contract to Canvas2D: ±1 LSB on R, G, B; alpha untouched.
  ApplyCanvas2DPixelNoise(pixels, seed);
}

void ApplyWebGLReadPixelsFloat(base::span<float> pixels, uint32_t seed) {
  for (size_t i = 0; i < pixels.size(); ++i) {
    Mulberry32 rng(seed ^ static_cast<uint32_t>(i));
    const uint32_t bit = rng.Next() & 1u;
    if (bit == 0u) {
      continue;
    }
    uint32_t bits = 0;
    static_assert(sizeof(float) == sizeof(uint32_t),
                  "ApplyWebGLReadPixelsFloat requires IEEE-754 32-bit float");
    std::memcpy(&bits, &pixels[i], sizeof(bits));
    bits ^= 1u;  // toggle the lowest mantissa bit (1 ULP)
    std::memcpy(&pixels[i], &bits, sizeof(bits));
  }
}

void ApplyWebGLReadPixelsNoise(base::span<uint8_t> pixels,
                                uint32_t format,
                                uint32_t type,
                                uint32_t seed) {
  if (type == kGlUnsignedByte &&
      (format == kGlRgba || format == kGlRgb)) {
    ApplyWebGLReadPixelsRGBA8(pixels, seed);
    return;
  }
  if (type == kGlFloat) {
    // Reinterpret as a span of float. WebGL's readPixels with FLOAT type
    // writes 4 bytes per scalar.
    if (pixels.size() < sizeof(float)) {
      return;
    }
    base::span<float> floats(reinterpret_cast<float*>(pixels.data()),
                              pixels.size() / sizeof(float));
    ApplyWebGLReadPixelsFloat(floats, seed);
    return;
  }
  // Unrecognized format: leave the buffer untouched. The spec's safety
  // contract here is "never corrupt rendered content"; a fingerprinting
  // probe that targets an unusual format gets the upstream value back, but
  // the seed-keyed Canvas2D / Float paths cover the common cases.
}

}  // namespace blink::ghostium_fp
