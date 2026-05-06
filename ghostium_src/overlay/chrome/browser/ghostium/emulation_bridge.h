// Copyright 2026 The Ghostium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GHOSTIUM_OVERLAY_CHROME_BROWSER_GHOSTIUM_EMULATION_BRIDGE_H_
#define GHOSTIUM_OVERLAY_CHROME_BROWSER_GHOSTIUM_EMULATION_BRIDGE_H_

#include "ghostium_src/overlay/chrome/browser/ghostium/fingerprint_profile.h"

namespace content {
class BrowserContext;
class WebContents;
}

namespace ghostium {

// Bridges fingerprint profile fields onto upstream Emulation-domain
// equivalents that Ghostium's own Blink hooks cannot reach.
//
// Currently covers ``WebContents::SetUserAgentOverride``: this single call
// routes both the network-layer User-Agent / Accept-Language /
// Sec-CH-UA-* headers and the Blink-side ``navigator.userAgent`` /
// ``navigator.userAgentData``. Other surfaces (timezone, locale,
// hardware concurrency, device metrics) are handled by the renderer-side
// hooks shipped in Specs E and F.
//
// All entry points run on the UI thread.
class EmulationBridge {
 public:
  // Apply the profile to every live WebContents in ``ctx``. WebContents
  // created later in the same context are picked up by
  // ``FingerprintProfileDelivery``'s per-WebContents observer hook
  // (``ApplyToWebContents``).
  static void Apply(content::BrowserContext* ctx,
                    const FingerprintProfile& profile);

  // Apply the profile to a single WebContents. Called from the per-frame
  // delivery path so that targets created after ``Apply`` still receive the
  // override on first navigation.
  static void ApplyToWebContents(content::WebContents* web_contents,
                                  const FingerprintProfile& profile);

  // Remove the UA override from every live WebContents in ``ctx``.
  static void Clear(content::BrowserContext* ctx);

  // Remove the UA override from a single WebContents. Called from the
  // delivery layer's per-WebContents iteration on ``OnProfileCleared``.
  static void ClearForWebContents(content::WebContents* web_contents);
};

}  // namespace ghostium

#endif  // GHOSTIUM_OVERLAY_CHROME_BROWSER_GHOSTIUM_EMULATION_BRIDGE_H_
