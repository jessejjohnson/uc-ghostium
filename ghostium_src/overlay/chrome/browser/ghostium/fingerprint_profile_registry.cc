// Copyright 2026 The Ghostium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ghostium_src/overlay/chrome/browser/ghostium/fingerprint_profile_registry.h"

#include <utility>

#include "base/check.h"
#include "content/public/browser/browser_thread.h"

namespace ghostium {

FingerprintProfileRegistry::FingerprintProfileRegistry(Profile* profile)
    : owning_profile_(profile) {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
}

FingerprintProfileRegistry::~FingerprintProfileRegistry() {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
}

void FingerprintProfileRegistry::SetProfile(content::BrowserContext* ctx,
                                            FingerprintProfile profile) {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
  CHECK(ctx);
  auto [it, _] = profiles_.insert_or_assign(ctx, std::move(profile));
  for (Observer& obs : observers_) {
    obs.OnProfileSet(ctx, it->second);
  }
}

void FingerprintProfileRegistry::UpdateProfile(content::BrowserContext* ctx,
                                               FingerprintProfile profile) {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
  CHECK(ctx);
  auto [it, _] = profiles_.insert_or_assign(ctx, std::move(profile));
  for (Observer& obs : observers_) {
    obs.OnProfileUpdated(ctx, it->second);
  }
}

void FingerprintProfileRegistry::ClearProfile(content::BrowserContext* ctx) {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
  CHECK(ctx);
  if (profiles_.erase(ctx) == 0) {
    return;
  }
  for (Observer& obs : observers_) {
    obs.OnProfileCleared(ctx);
  }
}

std::optional<FingerprintProfile> FingerprintProfileRegistry::GetProfile(
    content::BrowserContext* ctx) const {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
  auto it = profiles_.find(ctx);
  if (it == profiles_.end()) {
    return std::nullopt;
  }
  return it->second;
}

bool FingerprintProfileRegistry::HasProfile(
    content::BrowserContext* ctx) const {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
  return profiles_.contains(ctx);
}

void FingerprintProfileRegistry::AddObserver(Observer* obs) {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
  observers_.AddObserver(obs);
}

void FingerprintProfileRegistry::RemoveObserver(Observer* obs) {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
  observers_.RemoveObserver(obs);
}

}  // namespace ghostium
