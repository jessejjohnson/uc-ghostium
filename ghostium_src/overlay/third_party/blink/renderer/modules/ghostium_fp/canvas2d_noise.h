// Copyright 2026 The Ghostium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GHOSTIUM_OVERLAY_THIRD_PARTY_BLINK_RENDERER_MODULES_GHOSTIUM_FP_CANVAS2D_NOISE_H_
#define GHOSTIUM_OVERLAY_THIRD_PARTY_BLINK_RENDERER_MODULES_GHOSTIUM_FP_CANVAS2D_NOISE_H_

#include <cstdint>

#include "base/containers/span.h"

namespace blink::ghostium_fp {

// Apply deterministic ±1 LSB noise to the R, G, B channels of an RGBA byte
// buffer (as returned by Canvas2D readbacks via toDataURL / toBlob /
// getImageData). The alpha channel is left untouched per plan.md section
// 4.6 so that fully-transparent pixels remain fully-transparent.
//
// The buffer is interpreted as a packed RGBA8 sequence: every 4 bytes form
// one pixel ``(R, G, B, A)``. Buffers with a length that is not a multiple
// of 4 are clamped down to the nearest pixel boundary; the trailing bytes
// are left untouched.
//
// Determinism: for a given ``seed`` and a given input buffer, the output is
// always identical. The PRNG state is reseeded per-pixel as
// ``Mulberry32(seed ^ pixel_byte_offset)`` so different regions of the same
// buffer mutate independently and the function can run in any order without
// affecting the result.
//
// Saturation: per-channel additions saturate at 0 and 255 rather than
// wrapping. Thus a pixel at exactly 0 may stay at 0 (when the random delta
// is -1), and a pixel at exactly 255 may stay at 255.
void ApplyCanvas2DPixelNoise(base::span<uint8_t> rgba_bytes, uint32_t seed);

}  // namespace blink::ghostium_fp

#endif  // GHOSTIUM_OVERLAY_THIRD_PARTY_BLINK_RENDERER_MODULES_GHOSTIUM_FP_CANVAS2D_NOISE_H_
