// Copyright 2026 The Ghostium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ghostium_src/overlay/chrome/browser/ghostium/fingerprint_profile_delivery.h"

#include <utility>

#include "base/check.h"
#include "chrome/browser/profiles/profile.h"
#include "content/public/browser/browser_context.h"
#include "content/public/browser/browser_thread.h"
#include "content/public/browser/render_frame_host.h"
#include "content/public/browser/web_contents.h"
#include "content/public/browser/web_contents_observer.h"
#include "ghostium_src/overlay/chrome/browser/ghostium/emulation_bridge.h"
#include "ghostium_src/overlay/chrome/browser/ghostium/fingerprint_profile_mojom_traits.h"
#include "ghostium_src/overlay/chrome/browser/ghostium/fingerprint_profile_registry.h"
#include "ghostium_src/overlay/chrome/browser/ghostium/fingerprint_profile_registry_factory.h"
#include "mojo/public/cpp/bindings/associated_remote.h"
#include "third_party/blink/public/common/associated_interfaces/associated_interface_provider.h"
#include "third_party/blink/public/mojom/ghostium/fingerprint_profile.mojom.h"

namespace ghostium {

// Per-WebContents observer; hands every newly created RenderFrameHost to the
// owning FingerprintProfileDelivery so it can be primed with the context's
// current profile.
class FingerprintProfileDelivery::FrameObserver
    : public content::WebContentsObserver {
 public:
  FrameObserver(FingerprintProfileDelivery* owner,
                content::WebContents* web_contents)
      : content::WebContentsObserver(web_contents), owner_(owner) {}

  FrameObserver(const FrameObserver&) = delete;
  FrameObserver& operator=(const FrameObserver&) = delete;

  ~FrameObserver() override = default;

  // content::WebContentsObserver:
  void RenderFrameCreated(content::RenderFrameHost* rfh) override {
    DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
    if (!rfh || !owner_ || !owner_->registry_) {
      return;
    }
    auto profile = owner_->registry_->GetProfile(rfh->GetBrowserContext());
    if (!profile.has_value()) {
      // R16 invariant: non-Ghostium contexts do not bind the Mojo remote.
      return;
    }
    FingerprintProfileDelivery::PushProfileToFrame(rfh, *profile);
    if (rfh->IsInPrimaryMainFrame()) {
      EmulationBridge::ApplyToWebContents(web_contents(), *profile);
    }
  }

  content::WebContents* contents() { return web_contents(); }

 private:
  raw_ptr<FingerprintProfileDelivery> owner_;
};

FingerprintProfileDelivery::FingerprintProfileDelivery(Profile* profile)
    : profile_(profile) {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
  registry_ = FingerprintProfileRegistryFactory::GetForProfile(profile_);
  if (registry_) {
    registry_->AddObserver(this);
  }
}

FingerprintProfileDelivery::~FingerprintProfileDelivery() {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
  if (registry_) {
    registry_->RemoveObserver(this);
  }
}

void FingerprintProfileDelivery::ObserveWebContents(
    content::WebContents* web_contents) {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
  CHECK(web_contents);
  for (const auto& obs : frame_observers_) {
    if (obs->contents() == web_contents) {
      return;
    }
  }
  frame_observers_.push_back(
      std::make_unique<FrameObserver>(this, web_contents));
}

void FingerprintProfileDelivery::OnProfileSet(
    content::BrowserContext* ctx, const FingerprintProfile& profile) {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
  RepushForContext(ctx, profile);
}

void FingerprintProfileDelivery::OnProfileUpdated(
    content::BrowserContext* ctx, const FingerprintProfile& profile) {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
  RepushForContext(ctx, profile);
}

void FingerprintProfileDelivery::OnProfileCleared(
    content::BrowserContext* ctx) {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
  // Reset the UA override on every WebContents we track that belongs to the
  // cleared context. The Mojo profile cache held by FingerprintNoiseSource
  // outlives the registry record; the renderer keeps applying the prior
  // profile until navigation discards the supplement, which is acceptable
  // for the use case (clearBrowserContextFingerprint is followed in
  // practice by disposeOnDetach).
  for (const auto& obs : frame_observers_) {
    content::WebContents* wc = obs->contents();
    if (wc && wc->GetBrowserContext() == ctx) {
      EmulationBridge::ClearForWebContents(wc);
    }
  }
}

// static
void FingerprintProfileDelivery::PushProfileToFrame(
    content::RenderFrameHost* rfh, const FingerprintProfile& profile) {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
  if (!rfh) {
    return;
  }
  mojo::AssociatedRemote<blink::mojom::FingerprintProfileReceiver> remote;
  rfh->GetRemoteAssociatedInterfaces()->GetInterface(&remote);
  remote->SetProfile(ToMojo(profile));
}

void FingerprintProfileDelivery::RepushForContext(
    content::BrowserContext* ctx, const FingerprintProfile& profile) {
  for (const auto& obs : frame_observers_) {
    content::WebContents* wc = obs->contents();
    if (!wc || wc->GetBrowserContext() != ctx) {
      continue;
    }
    wc->ForEachRenderFrameHost([&profile](content::RenderFrameHost* rfh) {
      if (rfh && rfh->IsActive()) {
        FingerprintProfileDelivery::PushProfileToFrame(rfh, profile);
      }
    });
    EmulationBridge::ApplyToWebContents(wc, profile);
  }
}

}  // namespace ghostium
