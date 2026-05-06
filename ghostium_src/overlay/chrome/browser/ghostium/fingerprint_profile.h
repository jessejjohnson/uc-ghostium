// Copyright 2026 The Ghostium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GHOSTIUM_OVERLAY_CHROME_BROWSER_GHOSTIUM_FINGERPRINT_PROFILE_H_
#define GHOSTIUM_OVERLAY_CHROME_BROWSER_GHOSTIUM_FINGERPRINT_PROFILE_H_

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace ghostium {

// All fields optional. Absent means "pass through upstream default".
// This is the canonical in-process representation. The Mojo struct in
// //ghostium_src/overlay/public/mojom/ghostium/fingerprint_profile.mojom is a
// field-for-field mirror; see fingerprint_profile_mojom_traits.{h,cc} for the
// translation layer.
struct FingerprintProfile {
  FingerprintProfile();
  FingerprintProfile(const FingerprintProfile&);
  FingerprintProfile(FingerprintProfile&&) noexcept;
  FingerprintProfile& operator=(const FingerprintProfile&);
  FingerprintProfile& operator=(FingerprintProfile&&) noexcept;
  ~FingerprintProfile();

  // Navigator / UA
  std::optional<std::string> user_agent;

  struct UserAgentBrand {
    std::string brand;
    std::string version;
  };
  std::optional<std::vector<UserAgentBrand>> ua_brands;
  std::optional<std::vector<UserAgentBrand>> ua_full_version_list;
  std::optional<bool> ua_mobile;
  std::optional<std::string> ua_platform;
  std::optional<std::string> ua_platform_version;
  std::optional<std::string> ua_architecture;
  std::optional<std::string> ua_bitness;
  std::optional<std::string> ua_model;
  std::optional<std::string> ua_full_version;
  std::optional<bool> ua_wow64;

  // Navigator scalars
  std::optional<std::string> platform;
  std::optional<uint32_t> hardware_concurrency;
  // GiB rounded to 0.25, 0.5, 1, 2, 4, 8.
  std::optional<double> device_memory;
  std::optional<std::vector<std::string>> languages;

  // Webdriver. Default false when a profile is present.
  std::optional<bool> webdriver;

  // Screen + window
  struct ScreenSpec {
    uint32_t width = 0;
    uint32_t height = 0;
    uint32_t avail_width = 0;
    uint32_t avail_height = 0;
    uint32_t color_depth = 0;  // typically 24 or 30
    uint32_t pixel_depth = 0;  // equals color_depth
  };
  std::optional<ScreenSpec> screen;
  std::optional<double> device_pixel_ratio;

  // Timezone + locale
  std::optional<std::string> timezone;  // IANA, e.g. "America/Los_Angeles"
  std::optional<std::string> primary_language;  // e.g. "en-US"

  // Canvas / WebGL / Audio noise seeds
  std::optional<uint64_t> canvas_seed;
  std::optional<uint64_t> webgl_seed;
  std::optional<uint64_t> audio_seed;

  // WebGL debug renderer info
  std::optional<std::string> webgl_vendor;    // UNMASKED_VENDOR_WEBGL
  std::optional<std::string> webgl_renderer;  // UNMASKED_RENDERER_WEBGL

  // Fonts: if present, document.fonts and CSS @font-face matching restrict
  // enumeration to this whitelist (plus the always-present generic families:
  // serif, sans-serif, monospace, cursive, fantasy).
  std::optional<std::vector<std::string>> fonts_whitelist;

  // Plugins / MIME types: if present, navigator.plugins returns exactly these
  // entries. Empty vector returns an empty plugin list.
  struct PluginSpec {
    std::string name;
    std::string description;
    std::string filename;
    std::vector<std::string> mime_types;
  };
  std::optional<std::vector<PluginSpec>> plugins;

  // WebRTC ICE policy
  enum class WebRTCPolicy {
    kDefault,                     // upstream default
    kDefaultPublicInterfaceOnly,  // only default public interface
    kDisableNonProxiedUdp,        // force proxy for UDP
    kDisableBoth,                 // no UDP at all
  };
  std::optional<WebRTCPolicy> webrtc_policy;
  std::optional<bool> webrtc_mdns_only;  // surface only mdns candidates

  // MediaDevices enumeration override
  struct MediaDeviceSpec {
    std::string device_id;
    std::string kind;  // "audioinput" | "audiooutput" | "videoinput"
    std::string label;
    std::string group_id;
  };
  std::optional<std::vector<MediaDeviceSpec>> media_devices;
};

}  // namespace ghostium

#endif  // GHOSTIUM_OVERLAY_CHROME_BROWSER_GHOSTIUM_FINGERPRINT_PROFILE_H_
