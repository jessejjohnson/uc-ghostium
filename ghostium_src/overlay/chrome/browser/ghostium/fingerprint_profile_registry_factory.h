// Copyright 2026 The Ghostium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GHOSTIUM_OVERLAY_CHROME_BROWSER_GHOSTIUM_FINGERPRINT_PROFILE_REGISTRY_FACTORY_H_
#define GHOSTIUM_OVERLAY_CHROME_BROWSER_GHOSTIUM_FINGERPRINT_PROFILE_REGISTRY_FACTORY_H_

#include "base/no_destructor.h"
#include "chrome/browser/profiles/profile_keyed_service_factory.h"

class Profile;

namespace ghostium {

class FingerprintProfileRegistry;

// ProfileKeyedServiceFactory that uses
// ``ProfileSelections::BuildForRegularAndIncognito()`` so the registry is
// available on every Profile kind a CDP session can attach to (regular,
// primary OTR, independent OTR). Callers on the hot path assert with
// ``CHECK(registry)``.
class FingerprintProfileRegistryFactory final
    : public ProfileKeyedServiceFactory {
 public:
  static FingerprintProfileRegistry* GetForProfile(Profile* profile);
  static FingerprintProfileRegistryFactory* GetInstance();

  FingerprintProfileRegistryFactory(const FingerprintProfileRegistryFactory&) =
      delete;
  FingerprintProfileRegistryFactory& operator=(
      const FingerprintProfileRegistryFactory&) = delete;

 private:
  friend class base::NoDestructor<FingerprintProfileRegistryFactory>;

  FingerprintProfileRegistryFactory();
  ~FingerprintProfileRegistryFactory() override;

  // ProfileKeyedServiceFactory:
  std::unique_ptr<KeyedService> BuildServiceInstanceForBrowserContext(
      content::BrowserContext* context) const override;
};

}  // namespace ghostium

#endif  // GHOSTIUM_OVERLAY_CHROME_BROWSER_GHOSTIUM_FINGERPRINT_PROFILE_REGISTRY_FACTORY_H_
