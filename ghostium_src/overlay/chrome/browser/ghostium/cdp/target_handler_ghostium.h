// Copyright 2026 The Ghostium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GHOSTIUM_OVERLAY_CHROME_BROWSER_GHOSTIUM_CDP_TARGET_HANDLER_GHOSTIUM_H_
#define GHOSTIUM_OVERLAY_CHROME_BROWSER_GHOSTIUM_CDP_TARGET_HANDLER_GHOSTIUM_H_

#include <string>

#include "base/values.h"

namespace content {
class BrowserContext;
}

namespace ghostium::cdp {

// Result returned to the patched ``TargetHandler`` call site. ``ok`` is false
// when the input was malformed or the BrowserContext could not be resolved.
// ``error`` is suitable for ``Response::ServerError`` / ``InvalidParams``.
struct DispatchResult {
  bool ok = false;
  std::string error;
};

// ``Target.createBrowserContext``: the caller has already created the OTR
// BrowserContext via the standard path and is forwarding the optional
// ``ghostiumFingerprint`` parameter. Parses the dict, dispatches it to the
// ``FingerprintProfileRegistry``, and applies Emulation-domain bridges
// (UA override) to any WebContents that already exists in the context.
//
// Caller invariants:
//   * Runs on ``content::BrowserThread::UI``.
//   * ``ctx`` is a non-null, freshly created BrowserContext.
DispatchResult ApplyCreateFingerprint(content::BrowserContext* ctx,
                                       const base::Value::Dict& dict);

// ``Target.setBrowserContextFingerprint``: replace the active fingerprint
// for a live context. ``ctx`` must be the BrowserContext resolved from the
// caller's ``browserContextId``.
DispatchResult HandleSetFingerprint(content::BrowserContext* ctx,
                                     const base::Value::Dict& dict);

// ``Target.clearBrowserContextFingerprint``: remove the profile and clear
// any Emulation-domain UA override applied by ``ApplyCreateFingerprint`` /
// ``HandleSetFingerprint``.
DispatchResult HandleClearFingerprint(content::BrowserContext* ctx);

}  // namespace ghostium::cdp

#endif  // GHOSTIUM_OVERLAY_CHROME_BROWSER_GHOSTIUM_CDP_TARGET_HANDLER_GHOSTIUM_H_
