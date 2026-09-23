#include <gtest/gtest.h>

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
    uncommitted.flush_to_disk(tmp.wal_path());
  }

  simpledb::Database db(tmp.str());
  EXPECT_EQ(db.get("a"), std::optional<std::string>("1"));
  EXPECT_EQ(db.get("b"), std::optional<std::string>("2"));
}

}  // namespace
