// Copyright 2026 The Ghostium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ghostium_src/overlay/chrome/browser/ghostium/emulation_bridge.h"

#include "base/check.h"
#include "content/public/browser/browser_context.h"
#include "content/public/browser/browser_thread.h"
#include "content/public/browser/web_contents.h"
#include "third_party/blink/public/common/user_agent/user_agent_metadata.h"

namespace ghostium {

namespace {

// Build a blink::UserAgentOverride from the profile. Empty when the profile
// declares no UA fields; the caller treats an empty override as "do nothing".
blink::UserAgentOverride BuildOverride(const FingerprintProfile& profile) {
  blink::UserAgentOverride override;
  if (profile.user_agent.has_value()) {
    override.ua_string_override = *profile.user_agent;
  }

  // UA-CH metadata: only emit when at least one field is set; otherwise
  // upstream behavior remains in effect.
  bool has_ch = profile.ua_brands.has_value() ||
                profile.ua_full_version_list.has_value() ||
                profile.ua_mobile.has_value() ||
                profile.ua_platform.has_value() ||
                profile.ua_platform_version.has_value() ||
                profile.ua_architecture.has_value() ||
                profile.ua_bitness.has_value() ||
                profile.ua_model.has_value() ||
                profile.ua_full_version.has_value() ||
                profile.ua_wow64.has_value();
  if (has_ch) {
    blink::UserAgentMetadata md;
    if (profile.ua_brands.has_value()) {
      for (const auto& b : *profile.ua_brands) {
        md.brand_version_list.emplace_back(b.brand, b.version);
      }
    }
    if (profile.ua_full_version_list.has_value()) {
      for (const auto& b : *profile.ua_full_version_list) {
        md.brand_full_version_list.emplace_back(b.brand, b.version);
      }
    }
    if (profile.ua_full_version.has_value()) {
      md.full_version = *profile.ua_full_version;
    }
    if (profile.ua_platform.has_value()) {
      md.platform = *profile.ua_platform;
    }
    if (profile.ua_platform_version.has_value()) {
      md.platform_version = *profile.ua_platform_version;
    }
    if (profile.ua_architecture.has_value()) {
      md.architecture = *profile.ua_architecture;
    }
    if (profile.ua_model.has_value()) {
      md.model = *profile.ua_model;
    }
    md.mobile = profile.ua_mobile.value_or(false);
    if (profile.ua_bitness.has_value()) {
      md.bitness = *profile.ua_bitness;
    }
    md.wow64 = profile.ua_wow64.value_or(false);
    override.ua_metadata_override = std::move(md);
  }
  return override;
}

}  // namespace

// static
void EmulationBridge::Apply(content::BrowserContext* ctx,
                             const FingerprintProfile& profile) {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
  if (!ctx) {
    return;
  }
  // FingerprintProfileDelivery owns the per-WebContents tracker; it walks
  // its frame_observers_ list when re-pushing the Mojo profile (see
  // RepushForContext). The Emulation bridge is invoked once per profile
  // mutation at the CDP boundary; per-WebContents priming for newly
  // created targets goes through ``ApplyToWebContents`` from the frame
  // observer's RenderFrameCreated path.
  //
  // We deliberately do not iterate WebContents here: the
  // BrowserContext-wide enumeration would require cross-cutting access to
  // WebContentsImpl::GetAllWebContents. Instead, the delivery layer is
  // the single point of truth for "which WebContents belong to this
  // BrowserContext" and re-applies on profile mutation.
}

// static
void EmulationBridge::ApplyToWebContents(content::WebContents* web_contents,
                                          const FingerprintProfile& profile) {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
  if (!web_contents) {
    return;
  }
  blink::UserAgentOverride override = BuildOverride(profile);
  if (override.ua_string_override.empty() &&
      !override.ua_metadata_override.has_value()) {
    return;
  }
  web_contents->SetUserAgentOverride(override,
                                     /*override_in_new_navigations=*/true);
}

// static
void EmulationBridge::Clear(content::BrowserContext* ctx) {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
  // See ``Apply`` for the rationale: the delivery layer is responsible for
  // walking per-context WebContents on Registry::OnProfileCleared.
  if (!ctx) {
    return;
  }
}

// static
void EmulationBridge::ClearForWebContents(content::WebContents* web_contents) {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
  if (!web_contents) {
    return;
  }
  web_contents->SetUserAgentOverride(blink::UserAgentOverride(),
                                     /*override_in_new_navigations=*/true);
}

}  // namespace ghostium
