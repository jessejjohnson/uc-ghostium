// Copyright 2026 The Ghostium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GHOSTIUM_OVERLAY_CHROME_BROWSER_GHOSTIUM_FINGERPRINT_PROFILE_MOJOM_TRAITS_H_
#define GHOSTIUM_OVERLAY_CHROME_BROWSER_GHOSTIUM_FINGERPRINT_PROFILE_MOJOM_TRAITS_H_

#include "ghostium_src/overlay/chrome/browser/ghostium/fingerprint_profile.h"
#include "third_party/blink/public/mojom/ghostium/fingerprint_profile.mojom-forward.h"

namespace ghostium {

// Convert the browser-internal FingerprintProfile to the Mojo wire type that
// crosses the process boundary. The reverse direction is not required: the
// browser never receives a GhostiumFingerprintProfile over IPC.
//
// This function is the single translation point between ghostium:: and
// blink::mojom::; other browser-process code must not construct mojom types
// directly.
blink::mojom::GhostiumFingerprintProfilePtr ToMojo(
    const FingerprintProfile& profile);

}  // namespace ghostium

#endif  // GHOSTIUM_OVERLAY_CHROME_BROWSER_GHOSTIUM_FINGERPRINT_PROFILE_MOJOM_TRAITS_H_
