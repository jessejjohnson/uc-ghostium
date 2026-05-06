// Copyright 2026 The Ghostium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ghostium_src/overlay/chrome/browser/ghostium/fingerprint_profile_registry_factory.h"

#include "chrome/browser/profiles/profile.h"
#include "ghostium_src/overlay/chrome/browser/ghostium/fingerprint_profile_registry.h"

namespace ghostium {

// static
FingerprintProfileRegistry* FingerprintProfileRegistryFactory::GetForProfile(
    Profile* profile) {
  return static_cast<FingerprintProfileRegistry*>(
      GetInstance()->GetServiceForBrowserContext(profile, /*create=*/true));
}

// static
FingerprintProfileRegistryFactory*
FingerprintProfileRegistryFactory::GetInstance() {
  static base::NoDestructor<FingerprintProfileRegistryFactory> instance;
  return instance.get();
}

FingerprintProfileRegistryFactory::FingerprintProfileRegistryFactory()
    : ProfileKeyedServiceFactory(
          "GhostiumFingerprintProfileRegistry",
          ProfileSelections::Builder()
              .WithRegular(ProfileSelection::kOwnInstance)
              .WithGuest(ProfileSelection::kOwnInstance)
              .WithSystem(ProfileSelection::kNone)
              .Build()) {}

FingerprintProfileRegistryFactory::~FingerprintProfileRegistryFactory() =
    default;

std::unique_ptr<KeyedService>
FingerprintProfileRegistryFactory::BuildServiceInstanceForBrowserContext(
    content::BrowserContext* context) const {
  return std::make_unique<FingerprintProfileRegistry>(
      Profile::FromBrowserContext(context));
}

}  // namespace ghostium
