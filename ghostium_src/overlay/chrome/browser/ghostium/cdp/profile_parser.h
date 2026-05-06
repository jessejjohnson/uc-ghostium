// Copyright 2026 The Ghostium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GHOSTIUM_OVERLAY_CHROME_BROWSER_GHOSTIUM_CDP_PROFILE_PARSER_H_
#define GHOSTIUM_OVERLAY_CHROME_BROWSER_GHOSTIUM_CDP_PROFILE_PARSER_H_

#include <string>

#include "base/values.h"
#include "ghostium_src/overlay/chrome/browser/ghostium/fingerprint_profile.h"

namespace ghostium::cdp {

// Result of parsing the ``ghostiumFingerprint`` CDP parameter dict.
//
// On success, ``ok`` is true and ``profile`` holds the parsed value. On
// failure, ``ok`` is false and ``error`` holds a human-readable diagnostic
// suitable for returning as a CDP error response. ``profile`` is left in a
// default-constructed state.
struct ParseResult {
  bool ok = false;
  std::string error;
  FingerprintProfile profile;
};

// Parse the Ghostium fingerprint profile dict.
//
// Accepts the value of the ``ghostiumFingerprint`` parameter on
// ``Target.createBrowserContext`` or the body of
// ``Target.setBrowserContextFingerprint``. Every field is optional; an empty
// dict yields a default-constructed FingerprintProfile (every override
// absent, every hook a passthrough).
//
// Seed fields (``canvasSeed``, ``webglSeed``, ``audioSeed``) accept either:
//   * an integer up to 2**53 (CDP wire encoding is JSON number / double)
//   * a string ``"0xNNNN..."`` (lowercase hex, up to 16 hex digits) for the
//     full uint64 range
//
// The PDL declares these as a ``GhostiumFingerprintSeed`` object with two
// optional fields ``int`` / ``hex`` (see plan.md section 4.4); the parser
// also accepts a bare integer for ergonomics.
ParseResult ParseProfile(const base::Value::Dict& dict);

}  // namespace ghostium::cdp

#endif  // GHOSTIUM_OVERLAY_CHROME_BROWSER_GHOSTIUM_CDP_PROFILE_PARSER_H_
