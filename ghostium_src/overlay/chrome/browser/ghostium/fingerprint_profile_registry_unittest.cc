// Copyright 2026 The Ghostium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ghostium_src/overlay/chrome/browser/ghostium/fingerprint_profile_registry.h"

#include <optional>

#include "base/test/task_environment.h"
#include "chrome/test/base/testing_profile.h"
#include "content/public/test/browser_task_environment.h"
#include "ghostium_src/overlay/chrome/browser/ghostium/fingerprint_profile.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace ghostium {

namespace {

class RecordingObserver : public FingerprintProfileRegistry::Observer {
 public:
  struct Event {
    enum class Type { kSet, kUpdated, kCleared };
    Type type;
    content::BrowserContext* ctx;
    // Snapshot a small fingerprint: just the user_agent.
    std::optional<std::string> user_agent;
  };

  void OnProfileSet(content::BrowserContext* ctx,
                    const FingerprintProfile& profile) override {
    events.push_back({Event::Type::kSet, ctx, profile.user_agent});
  }
  void OnProfileUpdated(content::BrowserContext* ctx,
                        const FingerprintProfile& profile) override {
    events.push_back({Event::Type::kUpdated, ctx, profile.user_agent});
  }
  void OnProfileCleared(content::BrowserContext* ctx) override {
    events.push_back({Event::Type::kCleared, ctx, std::nullopt});
  }

  std::vector<Event> events;
};

FingerprintProfile MakeProfile(std::string ua) {
  FingerprintProfile p;
  p.user_agent = std::move(ua);
  return p;
}

class FingerprintProfileRegistryTest : public ::testing::Test {
 protected:
  content::BrowserTaskEnvironment task_env_;
  TestingProfile profile_;
  FingerprintProfileRegistry registry_{&profile_};
};

TEST_F(FingerprintProfileRegistryTest, SetInsertsAndNotifies) {
  RecordingObserver obs;
  registry_.AddObserver(&obs);

  EXPECT_FALSE(registry_.HasProfile(&profile_));
  registry_.SetProfile(&profile_, MakeProfile("UA-A"));
  EXPECT_TRUE(registry_.HasProfile(&profile_));

  auto stored = registry_.GetProfile(&profile_);
  ASSERT_TRUE(stored.has_value());
  EXPECT_EQ(stored->user_agent, "UA-A");

  ASSERT_EQ(obs.events.size(), 1u);
  EXPECT_EQ(obs.events[0].type, RecordingObserver::Event::Type::kSet);
  EXPECT_EQ(obs.events[0].ctx, &profile_);
  EXPECT_EQ(obs.events[0].user_agent, "UA-A");

  registry_.RemoveObserver(&obs);
}

TEST_F(FingerprintProfileRegistryTest, UpdateReplacesAndNotifies) {
  RecordingObserver obs;
  registry_.AddObserver(&obs);

  registry_.SetProfile(&profile_, MakeProfile("UA-A"));
  registry_.UpdateProfile(&profile_, MakeProfile("UA-B"));

  auto stored = registry_.GetProfile(&profile_);
  ASSERT_TRUE(stored.has_value());
  EXPECT_EQ(stored->user_agent, "UA-B");

  ASSERT_EQ(obs.events.size(), 2u);
  EXPECT_EQ(obs.events[1].type, RecordingObserver::Event::Type::kUpdated);
  EXPECT_EQ(obs.events[1].user_agent, "UA-B");

  registry_.RemoveObserver(&obs);
}

TEST_F(FingerprintProfileRegistryTest, ClearRemovesAndNotifies) {
  RecordingObserver obs;
  registry_.AddObserver(&obs);

  registry_.SetProfile(&profile_, MakeProfile("UA-A"));
  registry_.ClearProfile(&profile_);

  EXPECT_FALSE(registry_.HasProfile(&profile_));
  EXPECT_FALSE(registry_.GetProfile(&profile_).has_value());

  ASSERT_EQ(obs.events.size(), 2u);
  EXPECT_EQ(obs.events[1].type, RecordingObserver::Event::Type::kCleared);

  registry_.RemoveObserver(&obs);
}

TEST_F(FingerprintProfileRegistryTest, ClearOnUnknownContextIsNoOp) {
  RecordingObserver obs;
  registry_.AddObserver(&obs);

  TestingProfile other;
  registry_.ClearProfile(&other);

  EXPECT_TRUE(obs.events.empty());

  registry_.RemoveObserver(&obs);
}

TEST_F(FingerprintProfileRegistryTest, MultipleContextsAreIsolated) {
  TestingProfile other;

  registry_.SetProfile(&profile_, MakeProfile("UA-A"));
  registry_.SetProfile(&other, MakeProfile("UA-B"));

  auto a = registry_.GetProfile(&profile_);
  auto b = registry_.GetProfile(&other);

  ASSERT_TRUE(a.has_value());
  ASSERT_TRUE(b.has_value());
  EXPECT_EQ(a->user_agent, "UA-A");
  EXPECT_EQ(b->user_agent, "UA-B");

  registry_.ClearProfile(&profile_);
  EXPECT_FALSE(registry_.HasProfile(&profile_));
  EXPECT_TRUE(registry_.HasProfile(&other));
}

TEST_F(FingerprintProfileRegistryTest, ObserverRemovalStopsNotifications) {
  RecordingObserver obs;
  registry_.AddObserver(&obs);
  registry_.RemoveObserver(&obs);

  registry_.SetProfile(&profile_, MakeProfile("UA-A"));

  EXPECT_TRUE(obs.events.empty());
}

}  // namespace
}  // namespace ghostium
