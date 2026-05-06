// Copyright 2026 The Ghostium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ghostium_src/overlay/chrome/browser/ghostium/fingerprint_profile.h"

namespace ghostium {

FingerprintProfile::FingerprintProfile() = default;
FingerprintProfile::FingerprintProfile(const FingerprintProfile&) = default;
FingerprintProfile::FingerprintProfile(FingerprintProfile&&) noexcept = default;
FingerprintProfile& FingerprintProfile::operator=(const FingerprintProfile&) =
    default;
FingerprintProfile& FingerprintProfile::operator=(
    FingerprintProfile&&) noexcept = default;
FingerprintProfile::~FingerprintProfile() = default;

}  // namespace ghostium
