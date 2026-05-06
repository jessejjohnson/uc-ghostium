// Copyright 2026 The Ghostium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GHOSTIUM_OVERLAY_CHROME_BROWSER_GHOSTIUM_FINGERPRINT_PROFILE_REGISTRY_H_
#define GHOSTIUM_OVERLAY_CHROME_BROWSER_GHOSTIUM_FINGERPRINT_PROFILE_REGISTRY_H_

#include <optional>

#include "base/containers/flat_map.h"
#include "base/memory/raw_ptr.h"
#include "base/observer_list.h"
#include "base/observer_list_types.h"
#include "components/keyed_service/core/keyed_service.h"
#include "ghostium_src/overlay/chrome/browser/ghostium/fingerprint_profile.h"

namespace content {
class BrowserContext;
}

class Profile;

namespace ghostium {

// Stores a FingerprintProfile per BrowserContext and emits change events.
// Lives on the UI thread; entry points assert with DCHECK_CURRENTLY_ON.
//
// One instance per chrome::Profile; accessible via
// FingerprintProfileRegistryFactory::GetForProfile().
class FingerprintProfileRegistry : public KeyedService {
 public:
  class Observer : public base::CheckedObserver {
   public:
    virtual void OnProfileSet(content::BrowserContext* ctx,
                              const FingerprintProfile& profile) {}
    virtual void OnProfileUpdated(content::BrowserContext* ctx,
                                  const FingerprintProfile& profile) {}
    virtual void OnProfileCleared(content::BrowserContext* ctx) {}
  };

  explicit FingerprintProfileRegistry(Profile* profile);
  ~FingerprintProfileRegistry() override;

  FingerprintProfileRegistry(const FingerprintProfileRegistry&) = delete;
  FingerprintProfileRegistry& operator=(const FingerprintProfileRegistry&) =
      delete;

  void SetProfile(content::BrowserContext* ctx, FingerprintProfile profile);
  void UpdateProfile(content::BrowserContext* ctx, FingerprintProfile profile);
  void ClearProfile(content::BrowserContext* ctx);
  std::optional<FingerprintProfile> GetProfile(
      content::BrowserContext* ctx) const;
  bool HasProfile(content::BrowserContext* ctx) const;

  void AddObserver(Observer* obs);
  void RemoveObserver(Observer* obs);

 private:
  // Key: raw BrowserContext*. Registry is per-Profile; entries are cleared on
  // Profile shutdown. For OTR BrowserContexts created via CDP, the entry is
  // cleared when disposeOnDetach triggers teardown.
  base::flat_map<content::BrowserContext*, FingerprintProfile> profiles_;
  raw_ptr<Profile> owning_profile_;
  base::ObserverList<Observer> observers_;
};

}  // namespace ghostium

#endif  // GHOSTIUM_OVERLAY_CHROME_BROWSER_GHOSTIUM_FINGERPRINT_PROFILE_REGISTRY_H_
