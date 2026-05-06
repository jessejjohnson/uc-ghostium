// Copyright 2026 The Ghostium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GHOSTIUM_OVERLAY_THIRD_PARTY_BLINK_RENDERER_MODULES_GHOSTIUM_FP_MULBERRY32_H_
#define GHOSTIUM_OVERLAY_THIRD_PARTY_BLINK_RENDERER_MODULES_GHOSTIUM_FP_MULBERRY32_H_

#include <cstdint>

namespace blink {

// Allocation-free, deterministic, 32-bit state PRNG.
// Used to derive per-pixel / per-sample noise keyed by (profile seed ^ index).
// See plan.md section 4.6.
class Mulberry32 {
 public:
  explicit Mulberry32(uint32_t seed) : state_(seed) {}

  uint32_t Next() {
    uint32_t z = (state_ += 0x6D2B79F5u);
    z = (z ^ (z >> 15)) * (z | 1u);
    z ^= z + (z ^ (z >> 7)) * (z | 61u);
    return z ^ (z >> 14);
  }

 private:
  uint32_t state_;
};

}  // namespace blink

#endif  // GHOSTIUM_OVERLAY_THIRD_PARTY_BLINK_RENDERER_MODULES_GHOSTIUM_FP_MULBERRY32_H_
