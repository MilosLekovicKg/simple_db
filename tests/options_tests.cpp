#include <gtest/gtest.h>

#include <chrono>
#include <filesystem>
#include <stdexcept>
#include <thread>

#include "simple_db/database.hpp"
#include "simple_db/options.h"
#include "test_utils.h"

namespace {

TEST(OptionsTest, DefaultsMatchPreviousConstants) {
  const simpledb::Options options;
  EXPECT_EQ(options.durability, simpledb::DurabilityMode::kSync);
  EXPECT_EQ(options.snapshot_count_threshold, 100u);
  EXPECT_EQ(options.snapshot_byte_threshold, 1u << 20);
  EXPECT_EQ(options.snapshot_interval, std::chrono::milliseconds(30000));
  EXPECT_NO_THROW(options.validate());
}

TEST(OptionsTest, ValidateRejectsZeroSettings) {
  simpledb::Options options;
  options.snapshot_count_threshold = 0;
  EXPECT_THROW(options.validate(), std::invalid_argument);

  options = {};
  options.snapshot_byte_threshold = 0;
  EXPECT_THROW(options.validate(), std::invalid_argument);

  options = {};
  options.snapshot_interval = std::chrono::milliseconds(0);
  EXPECT_THROW(options.validate(), std::invalid_argument);
}

TEST(OptionsTest, DatabaseRejectsInvalidOptions) {
  simpledb::test::TempDbFile tmp("options_invalid");
  simpledb::Options options;
  options.snapshot_count_threshold = 0;
  EXPECT_THROW(simpledb::Database(tmp.str(), options), std::invalid_argument);
  EXPECT_FALSE(std::filesystem::exists(tmp.wal_path()));
}

TEST(OptionsTest, DatabaseExposesConfiguredOptions) {
  simpledb::test::TempDbFile tmp("options_exposed");
  simpledb::Options options;
  options.durability = simpledb::DurabilityMode::kAsync;
  options.snapshot_count_threshold = 7;
  simpledb::Database db(tmp.str(), options);
  EXPECT_EQ(db.options().durability, simpledb::DurabilityMode::kAsync);
  EXPECT_EQ(db.options().snapshot_count_threshold, 7u);
}

TEST(OptionsTest, CountThresholdTriggersBackgroundSnapshot) {
  simpledb::test::TempDbFile tmp("options_count");
  simpledb::Options options;
  options.durability = simpledb::DurabilityMode::kAsync;
  options.snapshot_count_threshold = 2;
  simpledb::Database db(tmp.str(), options);

  // The snapshot file only appears once a snapshot has run; with the default
  // threshold of 100, two writes would not create it before destruction.
  db.put("a", "1");
  db.put("b", "2");

  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
  while (!std::filesystem::exists(tmp.path()) &&
         std::chrono::steady_clock::now() < deadline) {
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }
  EXPECT_TRUE(std::filesystem::exists(tmp.path()));
}

TEST(OptionsTest, IntervalTriggersBackgroundSnapshot) {
  simpledb::test::TempDbFile tmp("options_interval");
  simpledb::Options options;
  options.durability = simpledb::DurabilityMode::kAsync;
  options.snapshot_interval = std::chrono::milliseconds(50);
  simpledb::Database db(tmp.str(), options);

  db.put("a", "1");

  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
  while (!std::filesystem::exists(tmp.path()) &&
         std::chrono::steady_clock::now() < deadline) {
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }
  EXPECT_TRUE(std::filesystem::exists(tmp.path()));
}

}  // namespace
