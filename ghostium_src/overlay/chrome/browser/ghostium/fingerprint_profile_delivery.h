// Copyright 2026 The Ghostium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GHOSTIUM_OVERLAY_CHROME_BROWSER_GHOSTIUM_FINGERPRINT_PROFILE_DELIVERY_H_
#define GHOSTIUM_OVERLAY_CHROME_BROWSER_GHOSTIUM_FINGERPRINT_PROFILE_DELIVERY_H_

#include <memory>
#include <vector>

#include "base/memory/raw_ptr.h"
#include "components/keyed_service/core/keyed_service.h"
#include "ghostium_src/overlay/chrome/browser/ghostium/fingerprint_profile_registry.h"

namespace content {
class BrowserContext;
class RenderFrameHost;
class WebContents;
}  // namespace content

class Profile;

namespace ghostium {

// Observes the FingerprintProfileRegistry and pushes profile mutations to
// every live frame in every WebContents owned by the associated Profile.
//
// Thread: UI only.
//
// Per-WebContents tracking lives in the nested FrameObserver class. On
// RenderFrameCreated, if the hosting BrowserContext has a registered
// profile, the profile is sent over a fire-and-forget
// ``blink::mojom::FingerprintProfileReceiver`` associated remote. If no
// profile is registered, the remote is never bound and the renderer-side
// ``FingerprintNoiseSource`` remains in passthrough mode (R16 invariant).
class FingerprintProfileDelivery : public KeyedService,
                                   public FingerprintProfileRegistry::Observer {
 public:
  explicit FingerprintProfileDelivery(Profile* profile);
  ~FingerprintProfileDelivery() override;

  FingerprintProfileDelivery(const FingerprintProfileDelivery&) = delete;
  FingerprintProfileDelivery& operator=(const FingerprintProfileDelivery&) =
      delete;

  // Attach a WebContents. No-op if already tracked.
  void ObserveWebContents(content::WebContents* web_contents);

  // FingerprintProfileRegistry::Observer:
  void OnProfileSet(content::BrowserContext* ctx,
                    const FingerprintProfile& profile) override;
  void OnProfileUpdated(content::BrowserContext* ctx,
                        const FingerprintProfile& profile) override;
  void OnProfileCleared(content::BrowserContext* ctx) override;

  // Push a profile to a single render frame. Made public so the CDP handler
  // can prime a target that arrives before the Registry dispatch completes.
  static void PushProfileToFrame(content::RenderFrameHost* rfh,
                                 const FingerprintProfile& profile);

 private:
  class FrameObserver;

  // Iterate every tracked WebContents whose hosting BrowserContext matches
  // ``ctx`` and re-push the profile to each live frame.
  void RepushForContext(content::BrowserContext* ctx,
                        const FingerprintProfile& profile);

  raw_ptr<Profile> profile_;
  raw_ptr<FingerprintProfileRegistry> registry_;
  std::vector<std::unique_ptr<FrameObserver>> frame_observers_;
};

}  // namespace ghostium

#endif  // GHOSTIUM_OVERLAY_CHROME_BROWSER_GHOSTIUM_FINGERPRINT_PROFILE_DELIVERY_H_
