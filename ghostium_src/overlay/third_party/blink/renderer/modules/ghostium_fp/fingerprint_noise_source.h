// Copyright 2026 The Ghostium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GHOSTIUM_OVERLAY_THIRD_PARTY_BLINK_RENDERER_MODULES_GHOSTIUM_FP_FINGERPRINT_NOISE_SOURCE_H_
#define GHOSTIUM_OVERLAY_THIRD_PARTY_BLINK_RENDERER_MODULES_GHOSTIUM_FP_FINGERPRINT_NOISE_SOURCE_H_

#include <cstdint>

#include "base/containers/span.h"
#include "third_party/blink/public/mojom/ghostium/fingerprint_profile.mojom-blink.h"
#include "third_party/blink/renderer/core/execution_context/execution_context_lifecycle_observer.h"
#include "third_party/blink/renderer/core/frame/local_dom_window.h"
#include "third_party/blink/renderer/modules/modules_export.h"
#include "third_party/blink/renderer/platform/heap/garbage_collected.h"
#include "third_party/blink/renderer/platform/mojo/heap_mojo_associated_receiver.h"
#include "third_party/blink/renderer/platform/supplementable.h"
#include "third_party/blink/renderer/platform/wtf/allocator/allocator.h"
#include "third_party/blink/renderer/platform/wtf/text/wtf_string.h"
#include "third_party/blink/renderer/platform/wtf/vector.h"

namespace blink {

class ImageData;

// Renderer-side Supplement that caches the active FingerprintProfile for a
// LocalDOMWindow. All hook sites route through this class: they either
// consult an ``*Override`` getter (returns false when there is no profile
// or the field is absent, meaning "pass through upstream default") or call
// an ``Apply*Noise`` method (no-op when IsActive() is false).
//
// Lifecycle: attached on first call to ``From()`` per window. Mojo receiver
// is bound eagerly so the browser can push a profile during
// RenderFrameCreated.
//
// Thread: Blink main thread only.
class MODULES_EXPORT FingerprintNoiseSource final
    : public GarbageCollected<FingerprintNoiseSource>,
      public Supplement<LocalDOMWindow>,
      public mojom::blink::FingerprintProfileReceiver {
 public:
  static const char kSupplementName[];

  static FingerprintNoiseSource& From(LocalDOMWindow& window);
  static FingerprintNoiseSource* FromIfExists(LocalDOMWindow& window);

  explicit FingerprintNoiseSource(LocalDOMWindow& window);

  FingerprintNoiseSource(const FingerprintNoiseSource&) = delete;
  FingerprintNoiseSource& operator=(const FingerprintNoiseSource&) = delete;

  // mojom::blink::FingerprintProfileReceiver:
  void SetProfile(mojom::blink::GhostiumFingerprintProfilePtr profile) override;

  bool IsActive() const { return profile_received_; }

  // Applied at hook sites. All are no-ops when IsActive() is false or the
  // relevant seed is absent from the cached profile (a profile may opt-in
  // to canvas noise without enabling webgl/audio, or vice versa).
  void ApplyCanvas2DNoise(ImageData* data) const;
  // Direct byte-buffer entry point: used by hook sites that have already
  // unwrapped the readback buffer (e.g. ``toDataURL`` post-encode).
  void ApplyCanvas2DPixelNoise(base::span<uint8_t> rgba_bytes) const;
  void ApplyWebGLReadPixelsNoise(base::span<uint8_t> pixels, uint32_t format,
                                 uint32_t type) const;
  void ApplyAudioNoise(base::span<float> samples,
                       uint32_t sample_index_base) const;
  // Byte-domain audio noise (AnalyserNode.getByte*Data).
  void ApplyAudioByteNoise(base::span<uint8_t> samples) const;

  // Override getters. Each returns true and writes ``*out`` when the profile
  // provides an explicit value for the field; otherwise returns false and
  // the caller must fall back to upstream behavior.
  bool WebDriverOverride(bool* out) const;
  bool UserAgentOverride(String* out) const;
  bool PlatformOverride(String* out) const;
  bool HardwareConcurrencyOverride(uint32_t* out) const;
  bool DeviceMemoryOverride(double* out) const;
  bool LanguagesOverride(Vector<String>* out) const;
  bool PrimaryLanguageOverride(String* out) const;
  bool ScreenWidthOverride(uint32_t* out) const;
  bool ScreenHeightOverride(uint32_t* out) const;
  bool ScreenAvailWidthOverride(uint32_t* out) const;
  bool ScreenAvailHeightOverride(uint32_t* out) const;
  bool ScreenColorDepthOverride(uint32_t* out) const;
  bool ScreenPixelDepthOverride(uint32_t* out) const;
  bool DevicePixelRatioOverride(double* out) const;
  bool TimezoneOverride(String* out) const;
  bool WebGLVendorOverride(String* out) const;
  bool WebGLRendererOverride(String* out) const;

  // Font and plugin predicates.
  // ``FontAllowed`` returns true if the profile does not restrict enumeration
  // (passthrough), or if the family is in the whitelist / is a generic
  // family. ``HasFontWhitelist`` reports whether any filtering is active.
  bool HasFontWhitelist() const;
  bool FontAllowed(const String& family) const;

  // Populate ``*out`` with a clone of the profile's plugin specs. Returns
  // true when the profile carries an explicit plugin list (including the
  // empty-list case, which is meaningful: it means ``navigator.plugins`` is
  // empty rather than passthrough). Returns false when the profile has no
  // plugins field set, in which case the caller falls back to upstream.
  bool PluginsOverride(
      WTF::Vector<mojom::blink::GhostiumPluginSpecPtr>* out) const;

  // Spec-G: MediaDevices.enumerateDevices override. Same shape as
  // ``PluginsOverride``: an explicit empty list reports true (the page
  // sees no devices) so a profile can deny enumeration without setting
  // every device kind individually.
  bool MediaDevicesOverride(
      WTF::Vector<mojom::blink::GhostiumMediaDeviceSpecPtr>* out) const;

  // Spec-G: WebRTC ICE policy. Returns true when the profile carries an
  // explicit ``webrtc_policy``; the consumer applies the policy at
  // RTCPeerConnection construction time.
  bool WebRTCPolicyOverride(mojom::blink::GhostiumWebRTCPolicy* out) const;

  // Spec-G: when true, ICE candidate gathering surfaces only mdns
  // candidates (suppressing host candidates with raw IPs). Returns true
  // when the profile carries an explicit ``webrtc_mdns_only`` value.
  bool WebRTCMdnsOnlyOverride(bool* out) const;

  void Trace(Visitor* visitor) const override;

 private:
  bool profile_received_ = false;
  mojom::blink::GhostiumFingerprintProfilePtr profile_;
  HeapMojoAssociatedReceiver<mojom::blink::FingerprintProfileReceiver,
                             FingerprintNoiseSource>
      receiver_;
};

}  // namespace blink

#endif  // GHOSTIUM_OVERLAY_THIRD_PARTY_BLINK_RENDERER_MODULES_GHOSTIUM_FP_FINGERPRINT_NOISE_SOURCE_H_
