#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <optional>
#include <string>

#include "simple_db/database.hpp"
#include "simple_db/logrecord.h"
#include "test_utils.h"

namespace {

using simpledb::test::TempDbFile;

// Records appended to the WAL beyond the snapshot's lsn are replayed on
// load.
TEST(RecoveryTest, WalRecordsBeyondSnapshotAreReplayed) {
  TempDbFile tmp("wal_replay");
  {
    simpledb::Database db(tmp.str());
    db.put("a", "1");

    // Simulate a write that reached the WAL but whose snapshot was never
    // taken (e.g. a crash right after append_to_wal), by appending a
    // record with an lsn beyond what this Database instance has committed.
    simpledb::LogRecord uncommitted(simpledb::LogRecordType::Put, "b", "2",
                                    999);
    uncommitted.flush_to_disk(tmp.wal_path(), simpledb::DurabilityMode::kSync);
  }

  simpledb::Database db(tmp.str());
  EXPECT_EQ(db.get("a"), std::optional<std::string>("1"));
  EXPECT_EQ(db.get("b"), std::optional<std::string>("2"));
}

// After a clean close the WAL is empty and the snapshot covers lsn N. Writes
// made after reopening must get lsn > N, otherwise recovery after a crash
// would mistake them for already-snapshotted records and drop them.
TEST(RecoveryTest, WritesAfterReopenSurviveCrashFollowingCleanClose) {
  TempDbFile tmp("lsn_reopen");
  TempDbFile crashed("lsn_reopen_crashed");
  {
    simpledb::Database db(tmp.str());
    db.put("a", "1");
    db.put("b", "2");
  }

  {
    simpledb::Database db(tmp.str());
    db.put("c", "3");

    // Copy the on-disk state while the database is still open: this is what
    // a crash right now would leave behind (no shutdown snapshot).
    std::filesystem::copy_file(tmp.path(), crashed.path());
    std::filesystem::copy_file(tmp.wal_path(), crashed.wal_path());
  }

  simpledb::Database recovered(crashed.str());
  EXPECT_EQ(recovered.get("a"), std::optional<std::string>("1"));
  EXPECT_EQ(recovered.get("b"), std::optional<std::string>("2"));
  EXPECT_EQ(recovered.get("c"), std::optional<std::string>("3"));
}

// A corrupt WAL tail must be removed during recovery even when no record was
// replayed; otherwise later appends sit behind it and are lost on the next
// recovery.
TEST(RecoveryTest, WritesAfterCorruptWalTailSurviveCrash) {
  TempDbFile tmp("torn_tail");
  TempDbFile crashed("torn_tail_crashed");
  {
    std::ofstream wal(tmp.wal_path(), std::ios::binary);
    const char garbage[] = {1, 2, 3};
    wal.write(garbage, sizeof(garbage));
  }
  {
    simpledb::Database db(tmp.str());
    db.put("x", "1");
    if (std::filesystem::exists(tmp.path())) {
      std::filesystem::copy_file(tmp.path(), crashed.path(),
                                 std::filesystem::copy_options::overwrite_existing);
    }
    std::filesystem::copy_file(tmp.wal_path(), crashed.wal_path(),
                               std::filesystem::copy_options::overwrite_existing);
  }
  simpledb::Database recovered(crashed.str());
  EXPECT_EQ(recovered.get("x"), std::optional<std::string>("1"));
}

}  // namespace
