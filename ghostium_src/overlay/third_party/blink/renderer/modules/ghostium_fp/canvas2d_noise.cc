// Copyright 2026 The Ghostium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ghostium_src/overlay/third_party/blink/renderer/modules/ghostium_fp/canvas2d_noise.h"

#include <algorithm>

#include "ghostium_src/overlay/third_party/blink/renderer/modules/ghostium_fp/mulberry32.h"

namespace blink::ghostium_fp {

void ApplyCanvas2DPixelNoise(base::span<uint8_t> rgba_bytes, uint32_t seed) {
  const size_t pixel_count = rgba_bytes.size() / 4;
  uint8_t* data = rgba_bytes.data();
  for (size_t i = 0; i < pixel_count; ++i) {
    const size_t byte_offset = i * 4;
    Mulberry32 rng(seed ^ static_cast<uint32_t>(byte_offset));
    for (size_t c = 0; c < 3; ++c) {
      const int delta = static_cast<int>(rng.Next() & 1u) * 2 - 1;
      const int v = static_cast<int>(data[byte_offset + c]) + delta;
      data[byte_offset + c] = static_cast<uint8_t>(std::clamp(v, 0, 255));
    }
    // data[byte_offset + 3] (alpha) deliberately untouched.
  }
}

}  // namespace blink::ghostium_fp
