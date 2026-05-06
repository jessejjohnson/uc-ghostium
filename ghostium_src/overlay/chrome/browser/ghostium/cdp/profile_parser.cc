// Copyright 2026 The Ghostium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ghostium_src/overlay/chrome/browser/ghostium/cdp/profile_parser.h"

#include <cstdint>
#include <limits>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "base/strings/string_number_conversions.h"
#include "base/strings/string_util.h"

namespace ghostium::cdp {

namespace {

constexpr double kMaxSafeInt = 9007199254740992.0;  // 2**53

// Returns nullopt for "field not present"; sets *err and returns nullopt for
// "field present but malformed".
std::optional<std::string> ReadString(const base::Value::Dict& dict,
                                       const char* key, std::string* err) {
  const base::Value* v = dict.Find(key);
  if (!v) {
    return std::nullopt;
  }
  if (!v->is_string()) {
    *err = std::string(key) + ": expected string";
    return std::nullopt;
  }
  return v->GetString();
}

std::optional<bool> ReadBool(const base::Value::Dict& dict, const char* key,
                              std::string* err) {
  const base::Value* v = dict.Find(key);
  if (!v) {
    return std::nullopt;
  }
  if (!v->is_bool()) {
    *err = std::string(key) + ": expected boolean";
    return std::nullopt;
  }
  return v->GetBool();
}

std::optional<double> ReadDouble(const base::Value::Dict& dict, const char* key,
                                  std::string* err) {
  const base::Value* v = dict.Find(key);
  if (!v) {
    return std::nullopt;
  }
  if (v->is_int()) {
    return static_cast<double>(v->GetInt());
  }
  if (v->is_double()) {
    return v->GetDouble();
  }
  *err = std::string(key) + ": expected number";
  return std::nullopt;
}

std::optional<uint32_t> ReadU32(const base::Value::Dict& dict, const char* key,
                                 std::string* err) {
  const base::Value* v = dict.Find(key);
  if (!v) {
    return std::nullopt;
  }
  double d = 0;
  if (v->is_int()) {
    d = static_cast<double>(v->GetInt());
  } else if (v->is_double()) {
    d = v->GetDouble();
  } else {
    *err = std::string(key) + ": expected integer";
    return std::nullopt;
  }
  if (d < 0 || d > std::numeric_limits<uint32_t>::max()) {
    *err = std::string(key) + ": out of range for uint32";
    return std::nullopt;
  }
  return static_cast<uint32_t>(d);
}

// Parse a seed field. Accepts either:
//   * A JSON number up to 2**53.
//   * An object {"int": N} or {"hex": "0xNN..."} per PDL.
//   * A bare hex string (ergonomic).
std::optional<uint64_t> ReadSeed(const base::Value::Dict& dict, const char* key,
                                  std::string* err) {
  const base::Value* v = dict.Find(key);
  if (!v) {
    return std::nullopt;
  }
  if (v->is_int()) {
    int64_t i = v->GetInt();
    if (i < 0) {
      *err = std::string(key) + ": negative seed";
      return std::nullopt;
    }
    return static_cast<uint64_t>(i);
  }
  if (v->is_double()) {
    double d = v->GetDouble();
    if (d < 0 || d > kMaxSafeInt) {
      *err = std::string(key) +
             ": seed magnitude exceeds 2**53; use the {hex: ...} form";
      return std::nullopt;
    }
    return static_cast<uint64_t>(d);
  }
  if (v->is_string()) {
    std::string s = v->GetString();
    if (!base::StartsWith(s, "0x", base::CompareCase::SENSITIVE)) {
      *err = std::string(key) + ": hex seed must start with 0x";
      return std::nullopt;
    }
    uint64_t out = 0;
    if (!base::HexStringToUInt64(s.substr(2), &out)) {
      *err = std::string(key) + ": malformed hex seed";
      return std::nullopt;
    }
    return out;
  }
  if (v->is_dict()) {
    const base::Value::Dict& seed = v->GetDict();
    if (const base::Value* iv = seed.Find("int")) {
      if (iv->is_int()) {
        int64_t i = iv->GetInt();
        if (i < 0) {
          *err = std::string(key) + ".int: negative seed";
          return std::nullopt;
        }
        return static_cast<uint64_t>(i);
      }
      if (iv->is_double()) {
        double d = iv->GetDouble();
        if (d < 0 || d > kMaxSafeInt) {
          *err = std::string(key) + ".int: exceeds 2**53";
          return std::nullopt;
        }
        return static_cast<uint64_t>(d);
      }
      *err = std::string(key) + ".int: expected number";
      return std::nullopt;
    }
    if (const std::string* hv = seed.FindString("hex")) {
      std::string s = *hv;
      if (!base::StartsWith(s, "0x", base::CompareCase::SENSITIVE)) {
        *err = std::string(key) + ".hex: must start with 0x";
        return std::nullopt;
      }
      uint64_t out = 0;
      if (!base::HexStringToUInt64(s.substr(2), &out)) {
        *err = std::string(key) + ".hex: malformed";
        return std::nullopt;
      }
      return out;
    }
    *err = std::string(key) + ": seed object missing 'int' or 'hex'";
    return std::nullopt;
  }
  *err = std::string(key) + ": expected number, hex string, or seed object";
  return std::nullopt;
}

std::optional<std::vector<std::string>> ReadStringArray(
    const base::Value::Dict& dict, const char* key, std::string* err) {
  const base::Value* v = dict.Find(key);
  if (!v) {
    return std::nullopt;
  }
  if (!v->is_list()) {
    *err = std::string(key) + ": expected array of string";
    return std::nullopt;
  }
  std::vector<std::string> out;
  out.reserve(v->GetList().size());
  for (const base::Value& item : v->GetList()) {
    if (!item.is_string()) {
      *err = std::string(key) + ": array contains non-string element";
      return std::nullopt;
    }
    out.push_back(item.GetString());
  }
  return out;
}

bool ReadScreen(const base::Value::Dict& dict, std::string* err,
                std::optional<FingerprintProfile::ScreenSpec>* out) {
  const base::Value* v = dict.Find("screen");
  if (!v) {
    return true;
  }
  if (!v->is_dict()) {
    *err = "screen: expected object";
    return false;
  }
  const base::Value::Dict& s = v->GetDict();
  FingerprintProfile::ScreenSpec spec;
  for (const auto& [field, target] : std::initializer_list<
           std::pair<const char*, uint32_t*>>{
           {"width", &spec.width},
           {"height", &spec.height},
           {"availWidth", &spec.avail_width},
           {"availHeight", &spec.avail_height},
           {"colorDepth", &spec.color_depth},
           {"pixelDepth", &spec.pixel_depth},
       }) {
    auto v32 = ReadU32(s, field, err);
    if (!err->empty()) {
      *err = std::string("screen.") + *err;
      return false;
    }
    if (v32.has_value()) {
      *target = *v32;
    }
  }
  *out = spec;
  return true;
}

bool ReadPlugins(const base::Value::Dict& dict, std::string* err,
                 std::optional<std::vector<FingerprintProfile::PluginSpec>>*
                     out) {
  const base::Value* v = dict.Find("plugins");
  if (!v) {
    return true;
  }
  if (!v->is_list()) {
    *err = "plugins: expected array";
    return false;
  }
  std::vector<FingerprintProfile::PluginSpec> plugins;
  plugins.reserve(v->GetList().size());
  for (const base::Value& item : v->GetList()) {
    if (!item.is_dict()) {
      *err = "plugins[]: expected object";
      return false;
    }
    const base::Value::Dict& d = item.GetDict();
    FingerprintProfile::PluginSpec p;
    if (const std::string* s = d.FindString("name")) {
      p.name = *s;
    }
    if (const std::string* s = d.FindString("description")) {
      p.description = *s;
    }
    if (const std::string* s = d.FindString("filename")) {
      p.filename = *s;
    }
    if (const base::Value::List* mts = d.FindList("mimeTypes")) {
      p.mime_types.reserve(mts->size());
      for (const base::Value& m : *mts) {
        if (!m.is_string()) {
          *err = "plugins[].mimeTypes: non-string element";
          return false;
        }
        p.mime_types.push_back(m.GetString());
      }
    }
    plugins.push_back(std::move(p));
  }
  *out = std::move(plugins);
  return true;
}

bool ReadMediaDevices(
    const base::Value::Dict& dict, std::string* err,
    std::optional<std::vector<FingerprintProfile::MediaDeviceSpec>>* out) {
  const base::Value* v = dict.Find("mediaDevices");
  if (!v) {
    return true;
  }
  if (!v->is_list()) {
    *err = "mediaDevices: expected array";
    return false;
  }
  std::vector<FingerprintProfile::MediaDeviceSpec> devs;
  devs.reserve(v->GetList().size());
  for (const base::Value& item : v->GetList()) {
    if (!item.is_dict()) {
      *err = "mediaDevices[]: expected object";
      return false;
    }
    const base::Value::Dict& d = item.GetDict();
    FingerprintProfile::MediaDeviceSpec spec;
    if (const std::string* s = d.FindString("deviceId")) {
      spec.device_id = *s;
    }
    if (const std::string* s = d.FindString("kind")) {
      spec.kind = *s;
    } else {
      *err = "mediaDevices[].kind: required";
      return false;
    }
    if (spec.kind != "audioinput" && spec.kind != "audiooutput" &&
        spec.kind != "videoinput") {
      *err = "mediaDevices[].kind: must be audioinput|audiooutput|videoinput";
      return false;
    }
    if (const std::string* s = d.FindString("label")) {
      spec.label = *s;
    }
    if (const std::string* s = d.FindString("groupId")) {
      spec.group_id = *s;
    }
    devs.push_back(std::move(spec));
  }
  *out = std::move(devs);
  return true;
}

bool ReadUserAgentMetadata(const base::Value::Dict& dict, std::string* err,
                           FingerprintProfile* profile) {
  const base::Value* v = dict.Find("userAgentMetadata");
  if (!v) {
    return true;
  }
  if (!v->is_dict()) {
    *err = "userAgentMetadata: expected object";
    return false;
  }
  const base::Value::Dict& m = v->GetDict();

  auto read_brands = [&](const char* key,
                          std::optional<std::vector<
                              FingerprintProfile::UserAgentBrand>>* out) {
    const base::Value* lv = m.Find(key);
    if (!lv) {
      return true;
    }
    if (!lv->is_list()) {
      *err = std::string("userAgentMetadata.") + key + ": expected array";
      return false;
    }
    std::vector<FingerprintProfile::UserAgentBrand> brands;
    brands.reserve(lv->GetList().size());
    for (const base::Value& b : lv->GetList()) {
      if (!b.is_dict()) {
        *err = std::string("userAgentMetadata.") + key + "[]: expected object";
        return false;
      }
      const base::Value::Dict& bd = b.GetDict();
      FingerprintProfile::UserAgentBrand brand;
      if (const std::string* s = bd.FindString("brand")) {
        brand.brand = *s;
      }
      if (const std::string* s = bd.FindString("version")) {
        brand.version = *s;
      }
      brands.push_back(std::move(brand));
    }
    *out = std::move(brands);
    return true;
  };

  if (!read_brands("brands", &profile->ua_brands)) {
    return false;
  }
  if (!read_brands("fullVersionList", &profile->ua_full_version_list)) {
    return false;
  }

  std::string scratch;
  profile->ua_mobile = ReadBool(m, "mobile", &scratch);
  if (!scratch.empty()) {
    *err = "userAgentMetadata." + scratch;
    return false;
  }
  profile->ua_platform = ReadString(m, "platform", &scratch);
  if (!scratch.empty()) {
    *err = "userAgentMetadata." + scratch;
    return false;
  }
  profile->ua_platform_version = ReadString(m, "platformVersion", &scratch);
  if (!scratch.empty()) {
    *err = "userAgentMetadata." + scratch;
    return false;
  }
  profile->ua_architecture = ReadString(m, "architecture", &scratch);
  if (!scratch.empty()) {
    *err = "userAgentMetadata." + scratch;
    return false;
  }
  profile->ua_bitness = ReadString(m, "bitness", &scratch);
  if (!scratch.empty()) {
    *err = "userAgentMetadata." + scratch;
    return false;
  }
  profile->ua_model = ReadString(m, "model", &scratch);
  if (!scratch.empty()) {
    *err = "userAgentMetadata." + scratch;
    return false;
  }
  profile->ua_full_version = ReadString(m, "fullVersion", &scratch);
  if (!scratch.empty()) {
    *err = "userAgentMetadata." + scratch;
    return false;
  }
  profile->ua_wow64 = ReadBool(m, "wow64", &scratch);
  if (!scratch.empty()) {
    *err = "userAgentMetadata." + scratch;
    return false;
  }
  return true;
}

bool ReadWebRTCPolicy(const base::Value::Dict& dict, std::string* err,
                      std::optional<FingerprintProfile::WebRTCPolicy>* out) {
  const base::Value* v = dict.Find("webrtcPolicy");
  if (!v) {
    return true;
  }
  if (!v->is_string()) {
    *err = "webrtcPolicy: expected string";
    return false;
  }
  const std::string& s = v->GetString();
  if (s == "default") {
    *out = FingerprintProfile::WebRTCPolicy::kDefault;
  } else if (s == "default_public_interface_only") {
    *out = FingerprintProfile::WebRTCPolicy::kDefaultPublicInterfaceOnly;
  } else if (s == "disable_non_proxied_udp") {
    *out = FingerprintProfile::WebRTCPolicy::kDisableNonProxiedUdp;
  } else if (s == "disable_both") {
    *out = FingerprintProfile::WebRTCPolicy::kDisableBoth;
  } else {
    *err = "webrtcPolicy: unknown value '" + s + "'";
    return false;
  }
  return true;
}

}  // namespace

ParseResult ParseProfile(const base::Value::Dict& dict) {
  ParseResult result;
  FingerprintProfile& p = result.profile;
  std::string err;

  p.user_agent = ReadString(dict, "userAgent", &err);
  if (!err.empty()) {
    result.error = err;
    return result;
  }

  if (!ReadUserAgentMetadata(dict, &err, &p)) {
    result.error = err;
    return result;
  }

  p.platform = ReadString(dict, "platform", &err);
  if (!err.empty()) {
    result.error = err;
    return result;
  }

  p.hardware_concurrency = ReadU32(dict, "hardwareConcurrency", &err);
  if (!err.empty()) {
    result.error = err;
    return result;
  }

  p.device_memory = ReadDouble(dict, "deviceMemory", &err);
  if (!err.empty()) {
    result.error = err;
    return result;
  }

  p.languages = ReadStringArray(dict, "languages", &err);
  if (!err.empty()) {
    result.error = err;
    return result;
  }

  p.webdriver = ReadBool(dict, "webdriver", &err);
  if (!err.empty()) {
    result.error = err;
    return result;
  }

  if (!ReadScreen(dict, &err, &p.screen)) {
    result.error = err;
    return result;
  }

  p.device_pixel_ratio = ReadDouble(dict, "devicePixelRatio", &err);
  if (!err.empty()) {
    result.error = err;
    return result;
  }

  p.timezone = ReadString(dict, "timezone", &err);
  if (!err.empty()) {
    result.error = err;
    return result;
  }
  p.primary_language = ReadString(dict, "primaryLanguage", &err);
  if (!err.empty()) {
    result.error = err;
    return result;
  }

  p.canvas_seed = ReadSeed(dict, "canvasSeed", &err);
  if (!err.empty()) {
    result.error = err;
    return result;
  }
  p.webgl_seed = ReadSeed(dict, "webglSeed", &err);
  if (!err.empty()) {
    result.error = err;
    return result;
  }
  p.audio_seed = ReadSeed(dict, "audioSeed", &err);
  if (!err.empty()) {
    result.error = err;
    return result;
  }

  p.webgl_vendor = ReadString(dict, "webglVendor", &err);
  if (!err.empty()) {
    result.error = err;
    return result;
  }
  p.webgl_renderer = ReadString(dict, "webglRenderer", &err);
  if (!err.empty()) {
    result.error = err;
    return result;
  }

  p.fonts_whitelist = ReadStringArray(dict, "fontsWhitelist", &err);
  if (!err.empty()) {
    result.error = err;
    return result;
  }

  if (!ReadPlugins(dict, &err, &p.plugins)) {
    result.error = err;
    return result;
  }

  if (!ReadWebRTCPolicy(dict, &err, &p.webrtc_policy)) {
    result.error = err;
    return result;
  }
  p.webrtc_mdns_only = ReadBool(dict, "webrtcMdnsOnly", &err);
  if (!err.empty()) {
    result.error = err;
    return result;
  }

  if (!ReadMediaDevices(dict, &err, &p.media_devices)) {
    result.error = err;
    return result;
  }

  result.ok = true;
  return result;
}

}  // namespace ghostium::cdp
