// Copyright 2026 The Ghostium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ghostium_src/overlay/third_party/blink/renderer/modules/ghostium_fp/fingerprint_noise_source.h"

#include <utility>

#include "base/check.h"
#include "ghostium_src/overlay/third_party/blink/renderer/modules/ghostium_fp/audio_noise.h"
#include "ghostium_src/overlay/third_party/blink/renderer/modules/ghostium_fp/canvas2d_noise.h"
#include "ghostium_src/overlay/third_party/blink/renderer/modules/ghostium_fp/webgl_noise.h"
#include "third_party/blink/public/common/associated_interfaces/associated_interface_provider.h"
#include "third_party/blink/renderer/core/dom/dom_token_list.h"
#include "third_party/blink/renderer/core/execution_context/execution_context.h"
#include "third_party/blink/renderer/core/html/canvas/image_data.h"
#include "third_party/blink/renderer/platform/scheduler/public/post_cross_thread_task.h"

namespace blink {

namespace {

bool IsGenericFamily(const String& family) {
  return family == "serif" || family == "sans-serif" || family == "monospace" ||
         family == "cursive" || family == "fantasy" || family == "system-ui" ||
         family == "math" || family == "emoji" || family == "fangsong";
}

}  // namespace

// static
const char FingerprintNoiseSource::kSupplementName[] =
    "GhostiumFingerprintNoiseSource";

// static
FingerprintNoiseSource& FingerprintNoiseSource::From(LocalDOMWindow& window) {
  DCHECK(IsMainThread());
  auto* supplement =
      Supplement<LocalDOMWindow>::From<FingerprintNoiseSource>(window);
  if (!supplement) {
    supplement = MakeGarbageCollected<FingerprintNoiseSource>(window);
    ProvideTo(window, supplement);
  }
  return *supplement;
}

// static
FingerprintNoiseSource* FingerprintNoiseSource::FromIfExists(
    LocalDOMWindow& window) {
  DCHECK(IsMainThread());
  return Supplement<LocalDOMWindow>::From<FingerprintNoiseSource>(window);
}

FingerprintNoiseSource::FingerprintNoiseSource(LocalDOMWindow& window)
    : Supplement<LocalDOMWindow>(window), receiver_(this, &window) {
  DCHECK(IsMainThread());
  // Bind the associated receiver eagerly. The browser pushes the profile
  // via ``RenderFrameHost::GetRemoteAssociatedInterfaces()`` on frame
  // creation; if no profile arrives, ``IsActive()`` stays false and every
  // hook remains a passthrough (R16).
  if (ExecutionContext* context = window.GetExecutionContext()) {
    auto task_runner = context->GetTaskRunner(TaskType::kInternalDefault);
    receiver_.Bind(
        window.GetAssociatedInterfaceProvider()
            ->GetInterface<mojom::blink::FingerprintProfileReceiver>(),
        task_runner);
  }
}

void FingerprintNoiseSource::SetProfile(
    mojom::blink::GhostiumFingerprintProfilePtr profile) {
  DCHECK(IsMainThread());
  profile_ = std::move(profile);
  profile_received_ = !profile_.is_null();
}

void FingerprintNoiseSource::Trace(Visitor* visitor) const {
  Supplement<LocalDOMWindow>::Trace(visitor);
  visitor->Trace(receiver_);
}

void FingerprintNoiseSource::ApplyCanvas2DNoise(ImageData* data) const {
  DCHECK(IsMainThread());
  if (!IsActive() || !data || !profile_->canvas_seed.has_value()) {
    return;
  }
  // ``ImageData::data()`` is a ``DOMUint8ClampedArray*``: its byte buffer is
  // a packed RGBA8 sequence we can mutate in place.
  if (auto* array = data->data()) {
    base::span<uint8_t> bytes(array->Data(),
                               static_cast<size_t>(array->length()));
    ghostium_fp::ApplyCanvas2DPixelNoise(
        bytes, static_cast<uint32_t>(*profile_->canvas_seed));
  }
}

void FingerprintNoiseSource::ApplyCanvas2DPixelNoise(
    base::span<uint8_t> rgba_bytes) const {
  DCHECK(IsMainThread());
  if (!IsActive() || !profile_->canvas_seed.has_value()) {
    return;
  }
  ghostium_fp::ApplyCanvas2DPixelNoise(
      rgba_bytes, static_cast<uint32_t>(*profile_->canvas_seed));
}

void FingerprintNoiseSource::ApplyWebGLReadPixelsNoise(
    base::span<uint8_t> pixels, uint32_t format, uint32_t type) const {
  DCHECK(IsMainThread());
  if (!IsActive() || !profile_->webgl_seed.has_value()) {
    return;
  }
  ghostium_fp::ApplyWebGLReadPixelsNoise(
      pixels, format, type, static_cast<uint32_t>(*profile_->webgl_seed));
}

void FingerprintNoiseSource::ApplyAudioNoise(base::span<float> samples,
                                             uint32_t sample_index_base) const {
  DCHECK(IsMainThread());
  if (!IsActive() || !profile_->audio_seed.has_value()) {
    return;
  }
  ghostium_fp::ApplyPcmFloatNoise(
      samples, static_cast<uint32_t>(*profile_->audio_seed),
      sample_index_base);
}

void FingerprintNoiseSource::ApplyAudioByteNoise(
    base::span<uint8_t> samples) const {
  DCHECK(IsMainThread());
  if (!IsActive() || !profile_->audio_seed.has_value()) {
    return;
  }
  ghostium_fp::ApplyByteAudioNoise(
      samples, static_cast<uint32_t>(*profile_->audio_seed));
}

bool FingerprintNoiseSource::WebDriverOverride(bool* out) const {
  DCHECK(IsMainThread());
  if (!profile_received_ || !profile_->webdriver.has_value()) {
    return false;
  }
  *out = *profile_->webdriver;
  return true;
}

bool FingerprintNoiseSource::UserAgentOverride(String* out) const {
  DCHECK(IsMainThread());
  if (!profile_received_ || !profile_->user_agent.has_value()) {
    return false;
  }
  *out = *profile_->user_agent;
  return true;
}

bool FingerprintNoiseSource::PlatformOverride(String* out) const {
  DCHECK(IsMainThread());
  if (!profile_received_ || !profile_->platform.has_value()) {
    return false;
  }
  *out = *profile_->platform;
  return true;
}

bool FingerprintNoiseSource::HardwareConcurrencyOverride(uint32_t* out) const {
  DCHECK(IsMainThread());
  if (!profile_received_ || !profile_->hardware_concurrency.has_value()) {
    return false;
  }
  *out = *profile_->hardware_concurrency;
  return true;
}

bool FingerprintNoiseSource::DeviceMemoryOverride(double* out) const {
  DCHECK(IsMainThread());
  if (!profile_received_ || !profile_->device_memory.has_value()) {
    return false;
  }
  *out = *profile_->device_memory;
  return true;
}

bool FingerprintNoiseSource::LanguagesOverride(Vector<String>* out) const {
  DCHECK(IsMainThread());
  if (!profile_received_ || !profile_->languages.has_value()) {
    return false;
  }
  out->clear();
  out->ReserveInitialCapacity(
      static_cast<wtf_size_t>(profile_->languages->size()));
  for (const auto& lang : *profile_->languages) {
    out->push_back(lang);
  }
  return true;
}

bool FingerprintNoiseSource::PrimaryLanguageOverride(String* out) const {
  DCHECK(IsMainThread());
  if (!profile_received_ || !profile_->primary_language.has_value()) {
    return false;
  }
  *out = *profile_->primary_language;
  return true;
}

namespace {

const blink::mojom::blink::GhostiumScreenSpecPtr* ScreenSpec(
    const mojom::blink::GhostiumFingerprintProfilePtr& profile) {
  if (!profile || profile->screen.is_null()) {
    return nullptr;
  }
  return &profile->screen;
}

}  // namespace

bool FingerprintNoiseSource::ScreenWidthOverride(uint32_t* out) const {
  DCHECK(IsMainThread());
  if (!profile_received_) {
    return false;
  }
  auto* s = ScreenSpec(profile_);
  if (!s) {
    return false;
  }
  *out = (*s)->width;
  return true;
}

bool FingerprintNoiseSource::ScreenHeightOverride(uint32_t* out) const {
  DCHECK(IsMainThread());
  if (!profile_received_) {
    return false;
  }
  auto* s = ScreenSpec(profile_);
  if (!s) {
    return false;
  }
  *out = (*s)->height;
  return true;
}

bool FingerprintNoiseSource::ScreenAvailWidthOverride(uint32_t* out) const {
  DCHECK(IsMainThread());
  if (!profile_received_) {
    return false;
  }
  auto* s = ScreenSpec(profile_);
  if (!s) {
    return false;
  }
  *out = (*s)->avail_width;
  return true;
}

bool FingerprintNoiseSource::ScreenAvailHeightOverride(uint32_t* out) const {
  DCHECK(IsMainThread());
  if (!profile_received_) {
    return false;
  }
  auto* s = ScreenSpec(profile_);
  if (!s) {
    return false;
  }
  *out = (*s)->avail_height;
  return true;
}

bool FingerprintNoiseSource::ScreenColorDepthOverride(uint32_t* out) const {
  DCHECK(IsMainThread());
  if (!profile_received_) {
    return false;
  }
  auto* s = ScreenSpec(profile_);
  if (!s) {
    return false;
  }
  *out = (*s)->color_depth;
  return true;
}

bool FingerprintNoiseSource::ScreenPixelDepthOverride(uint32_t* out) const {
  DCHECK(IsMainThread());
  if (!profile_received_) {
    return false;
  }
  auto* s = ScreenSpec(profile_);
  if (!s) {
    return false;
  }
  *out = (*s)->pixel_depth;
  return true;
}

bool FingerprintNoiseSource::DevicePixelRatioOverride(double* out) const {
  DCHECK(IsMainThread());
  if (!profile_received_ || !profile_->device_pixel_ratio.has_value()) {
    return false;
  }
  *out = *profile_->device_pixel_ratio;
  return true;
}

bool FingerprintNoiseSource::TimezoneOverride(String* out) const {
  DCHECK(IsMainThread());
  if (!profile_received_ || !profile_->timezone.has_value()) {
    return false;
  }
  *out = *profile_->timezone;
  return true;
}

bool FingerprintNoiseSource::WebGLVendorOverride(String* out) const {
  DCHECK(IsMainThread());
  if (!profile_received_ || !profile_->webgl_vendor.has_value()) {
    return false;
  }
  *out = *profile_->webgl_vendor;
  return true;
}

bool FingerprintNoiseSource::WebGLRendererOverride(String* out) const {
  DCHECK(IsMainThread());
  if (!profile_received_ || !profile_->webgl_renderer.has_value()) {
    return false;
  }
  *out = *profile_->webgl_renderer;
  return true;
}

bool FingerprintNoiseSource::HasFontWhitelist() const {
  DCHECK(IsMainThread());
  return profile_received_ && profile_->fonts_whitelist.has_value();
}

bool FingerprintNoiseSource::FontAllowed(const String& family) const {
  DCHECK(IsMainThread());
  if (!HasFontWhitelist()) {
    return true;
  }
  if (IsGenericFamily(family)) {
    return true;
  }
  for (const auto& allowed : *profile_->fonts_whitelist) {
    if (allowed == family) {
      return true;
    }
  }
  return false;
}

bool FingerprintNoiseSource::PluginsOverride(
    WTF::Vector<mojom::blink::GhostiumPluginSpecPtr>* out) const {
  DCHECK(IsMainThread());
  if (!profile_received_ || !profile_->plugins.has_value()) {
    return false;
  }
  out->clear();
  out->ReserveInitialCapacity(
      static_cast<wtf_size_t>(profile_->plugins->size()));
  for (const auto& p : *profile_->plugins) {
    out->push_back(p ? p->Clone() : mojom::blink::GhostiumPluginSpecPtr());
  }
  return true;
}

}  // namespace blink
