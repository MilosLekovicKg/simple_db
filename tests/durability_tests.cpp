#include <gtest/gtest.h>

#include <optional>
#include <string>
#include <system_error>
#include <vector>

#include "simple_db/database.hpp"
#include "simple_db/durability.h"
#include "simple_db/logrecord.h"
#include "simple_db/wal.h"
#include "test_utils.h"

namespace {

using simpledb::test::TempDbFile;

// Default Database construction is durable: a fresh WAL append survives
// reload without any explicit durability mode at the call site.
TEST(DurabilityTest, DatabaseDefaultsToSyncDurability) {
  TempDbFile tmp("durability_default");
  {
    simpledb::Database db(tmp.str());
    db.put("a", "1");
  }

  simpledb::Database db(tmp.str());
  EXPECT_EQ(db.get("a"), std::optional<std::string>("1"));
}

// Async and sync WAL appends are logically equivalent on replay; the
// durability mode only changes when bytes are guaranteed to hit disk, not
// what ends up in the WAL under normal (non-crash) operation.
TEST(DurabilityTest, AsyncAndSyncProduceSameReplayedState) {
  TempDbFile async_tmp("durability_async");
  TempDbFile sync_tmp("durability_sync");

  {
    simpledb::WAL async_wal(async_tmp.wal_path(), simpledb::DurabilityMode::kAsync);
    simpledb::WAL sync_wal(sync_tmp.wal_path(), simpledb::DurabilityMode::kSync);
    for (int i = 0; i < 10; ++i) {
      const std::string key = "key" + std::to_string(i);
      const std::string value = "value" + std::to_string(i);
      async_wal.append_to_wal(simpledb::LogRecordType::Put, key, value);
      sync_wal.append_to_wal(simpledb::LogRecordType::Put, key, value);
    }
  }

  std::vector<std::pair<std::string, std::string>> async_records;
  std::vector<std::pair<std::string, std::string>> sync_records;
  simpledb::WAL async_wal(async_tmp.wal_path(), simpledb::DurabilityMode::kAsync);
  simpledb::WAL sync_wal(sync_tmp.wal_path(), simpledb::DurabilityMode::kSync);
  async_wal.replay([&](const simpledb::LogRecord& record) {
    async_records.emplace_back(record.key(), record.value());
  });
  sync_wal.replay([&](const simpledb::LogRecord& record) {
    sync_records.emplace_back(record.key(), record.value());
  });

  EXPECT_EQ(async_records, sync_records);
}

// A sync-mode append must be immediately visible on reload, independent of
// any snapshot/compaction; this is the durability guarantee this feature
// exists to provide.
TEST(DurabilityTest, SyncAppendSurvivesReload) {
  TempDbFile tmp("durability_sync_reload");
  {
    simpledb::WAL wal(tmp.wal_path(), simpledb::DurabilityMode::kSync);
    wal.append_to_wal(simpledb::LogRecordType::Put, "durable", "yes");
  }

  simpledb::WAL wal(tmp.wal_path(), simpledb::DurabilityMode::kSync);
  std::vector<std::string> keys;
  wal.replay([&](const simpledb::LogRecord& record) { keys.push_back(record.key()); });

  ASSERT_EQ(keys.size(), size_t{1});
  EXPECT_EQ(keys[0], "durable");
}

// Appending to a path whose directory does not exist cannot open the file
// and must fail loudly rather than silently dropping the record.
TEST(DurabilityTest, AppendToUnwritablePathThrows) {
  simpledb::LogRecord record(simpledb::LogRecordType::Put, "a", "1");
  EXPECT_THROW(
      record.flush_to_disk("no_such_directory/durability.wal", simpledb::DurabilityMode::kSync),
      std::system_error);
}

}  // namespace
