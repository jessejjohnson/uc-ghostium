// Copyright 2026 The Ghostium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ghostium_src/overlay/chrome/browser/ghostium/cdp/target_handler_ghostium.h"

#include <utility>

#include "base/check.h"
#include "chrome/browser/profiles/profile.h"
#include "content/public/browser/browser_context.h"
#include "content/public/browser/browser_thread.h"
#include "ghostium_src/overlay/chrome/browser/ghostium/cdp/profile_parser.h"
#include "ghostium_src/overlay/chrome/browser/ghostium/emulation_bridge.h"
#include "ghostium_src/overlay/chrome/browser/ghostium/fingerprint_profile_registry.h"
#include "ghostium_src/overlay/chrome/browser/ghostium/fingerprint_profile_registry_factory.h"

namespace ghostium::cdp {

namespace {

FingerprintProfileRegistry* GetRegistry(content::BrowserContext* ctx) {
  Profile* p = Profile::FromBrowserContext(ctx);
  if (!p) {
    return nullptr;
  }
  return FingerprintProfileRegistryFactory::GetForProfile(p);
}

}  // namespace

DispatchResult ApplyCreateFingerprint(content::BrowserContext* ctx,
                                       const base::Value::Dict& dict) {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
  DispatchResult out;
  if (!ctx) {
    out.error = "ghostiumFingerprint: BrowserContext not provided";
    return out;
  }

  ParseResult parsed = ParseProfile(dict);
  if (!parsed.ok) {
    out.error = parsed.error;
    return out;
  }

  FingerprintProfileRegistry* registry = GetRegistry(ctx);
  if (!registry) {
    out.error = "ghostiumFingerprint: registry unavailable for context";
    return out;
  }

  registry->SetProfile(ctx, parsed.profile);
  ::ghostium::EmulationBridge::Apply(ctx, parsed.profile);

  out.ok = true;
  return out;
}

DispatchResult HandleSetFingerprint(content::BrowserContext* ctx,
                                     const base::Value::Dict& dict) {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
  DispatchResult out;
  if (!ctx) {
    out.error = "setBrowserContextFingerprint: unknown browserContextId";
    return out;
  }

  ParseResult parsed = ParseProfile(dict);
  if (!parsed.ok) {
    out.error = parsed.error;
    return out;
  }

  FingerprintProfileRegistry* registry = GetRegistry(ctx);
  if (!registry) {
    out.error = "setBrowserContextFingerprint: registry unavailable";
    return out;
  }
  if (!registry->HasProfile(ctx)) {
    // Set vs. update is mostly cosmetic for callers, but at the CDP boundary
    // we treat ``set`` on a never-fingerprinted context as ``Set`` rather
    // than ``Update`` so Registry observers see the right event type.
    registry->SetProfile(ctx, parsed.profile);
  } else {
    registry->UpdateProfile(ctx, parsed.profile);
  }
  ::ghostium::EmulationBridge::Apply(ctx, parsed.profile);

  out.ok = true;
  return out;
}

DispatchResult HandleClearFingerprint(content::BrowserContext* ctx) {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
  DispatchResult out;
  if (!ctx) {
    out.error = "clearBrowserContextFingerprint: unknown browserContextId";
    return out;
  }
  FingerprintProfileRegistry* registry = GetRegistry(ctx);
  if (!registry) {
    out.error = "clearBrowserContextFingerprint: registry unavailable";
    return out;
  }
  registry->ClearProfile(ctx);
  ::ghostium::EmulationBridge::Clear(ctx);
  out.ok = true;
  return out;
}

}  // namespace ghostium::cdp
