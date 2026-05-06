// Copyright 2026 The Ghostium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ghostium_src/overlay/chrome/browser/ghostium/fingerprint_profile_mojom_traits.h"

#include <utility>
#include <vector>

#include "third_party/blink/public/mojom/ghostium/fingerprint_profile.mojom.h"

namespace ghostium {

namespace {

std::vector<blink::mojom::GhostiumUserAgentBrandPtr> ToMojoBrands(
    const std::vector<FingerprintProfile::UserAgentBrand>& src) {
  std::vector<blink::mojom::GhostiumUserAgentBrandPtr> out;
  out.reserve(src.size());
  for (const auto& b : src) {
    auto brand = blink::mojom::GhostiumUserAgentBrand::New();
    brand->brand = b.brand;
    brand->version = b.version;
    out.push_back(std::move(brand));
  }
  return out;
}

blink::mojom::GhostiumScreenSpecPtr ToMojoScreen(
    const FingerprintProfile::ScreenSpec& s) {
  auto out = blink::mojom::GhostiumScreenSpec::New();
  out->width = s.width;
  out->height = s.height;
  out->avail_width = s.avail_width;
  out->avail_height = s.avail_height;
  out->color_depth = s.color_depth;
  out->pixel_depth = s.pixel_depth;
  return out;
}

std::vector<blink::mojom::GhostiumPluginSpecPtr> ToMojoPlugins(
    const std::vector<FingerprintProfile::PluginSpec>& src) {
  std::vector<blink::mojom::GhostiumPluginSpecPtr> out;
  out.reserve(src.size());
  for (const auto& p : src) {
    auto plugin = blink::mojom::GhostiumPluginSpec::New();
    plugin->name = p.name;
    plugin->description = p.description;
    plugin->filename = p.filename;
    plugin->mime_types = p.mime_types;
    out.push_back(std::move(plugin));
  }
  return out;
}

std::vector<blink::mojom::GhostiumMediaDeviceSpecPtr> ToMojoMediaDevices(
    const std::vector<FingerprintProfile::MediaDeviceSpec>& src) {
  std::vector<blink::mojom::GhostiumMediaDeviceSpecPtr> out;
  out.reserve(src.size());
  for (const auto& d : src) {
    auto dev = blink::mojom::GhostiumMediaDeviceSpec::New();
    dev->device_id = d.device_id;
    dev->kind = d.kind;
    dev->label = d.label;
    dev->group_id = d.group_id;
    out.push_back(std::move(dev));
  }
  return out;
}

blink::mojom::GhostiumWebRTCPolicy ToMojoWebRTCPolicy(
    FingerprintProfile::WebRTCPolicy p) {
  switch (p) {
    case FingerprintProfile::WebRTCPolicy::kDefault:
      return blink::mojom::GhostiumWebRTCPolicy::kDefault;
    case FingerprintProfile::WebRTCPolicy::kDefaultPublicInterfaceOnly:
      return blink::mojom::GhostiumWebRTCPolicy::kDefaultPublicInterfaceOnly;
    case FingerprintProfile::WebRTCPolicy::kDisableNonProxiedUdp:
      return blink::mojom::GhostiumWebRTCPolicy::kDisableNonProxiedUdp;
    case FingerprintProfile::WebRTCPolicy::kDisableBoth:
      return blink::mojom::GhostiumWebRTCPolicy::kDisableBoth;
  }
  return blink::mojom::GhostiumWebRTCPolicy::kDefault;
}

}  // namespace

blink::mojom::GhostiumFingerprintProfilePtr ToMojo(
    const FingerprintProfile& profile) {
  auto out = blink::mojom::GhostiumFingerprintProfile::New();

  out->user_agent = profile.user_agent;
  if (profile.ua_brands.has_value()) {
    out->ua_brands = ToMojoBrands(*profile.ua_brands);
  }
  if (profile.ua_full_version_list.has_value()) {
    out->ua_full_version_list = ToMojoBrands(*profile.ua_full_version_list);
  }
  out->ua_mobile = profile.ua_mobile;
  out->ua_platform = profile.ua_platform;
  out->ua_platform_version = profile.ua_platform_version;
  out->ua_architecture = profile.ua_architecture;
  out->ua_bitness = profile.ua_bitness;
  out->ua_model = profile.ua_model;
  out->ua_full_version = profile.ua_full_version;
  out->ua_wow64 = profile.ua_wow64;

  out->platform = profile.platform;
  out->hardware_concurrency = profile.hardware_concurrency;
  out->device_memory = profile.device_memory;
  out->languages = profile.languages;

  out->webdriver = profile.webdriver;
  if (profile.screen.has_value()) {
    out->screen = ToMojoScreen(*profile.screen);
  }
  out->device_pixel_ratio = profile.device_pixel_ratio;

  out->timezone = profile.timezone;
  out->primary_language = profile.primary_language;

  out->canvas_seed = profile.canvas_seed;
  out->webgl_seed = profile.webgl_seed;
  out->audio_seed = profile.audio_seed;

  out->webgl_vendor = profile.webgl_vendor;
  out->webgl_renderer = profile.webgl_renderer;

  out->fonts_whitelist = profile.fonts_whitelist;
  if (profile.plugins.has_value()) {
    out->plugins = ToMojoPlugins(*profile.plugins);
  }

  if (profile.webrtc_policy.has_value()) {
    out->webrtc_policy = ToMojoWebRTCPolicy(*profile.webrtc_policy);
  }
  out->webrtc_mdns_only = profile.webrtc_mdns_only;
  if (profile.media_devices.has_value()) {
    out->media_devices = ToMojoMediaDevices(*profile.media_devices);
  }

  return out;
}

}  // namespace ghostium
