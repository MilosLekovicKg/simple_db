#include "simple_db/storage.h"

#include <gtest/gtest.h>

#include <cstdint>
#include <fstream>

#include "simple_db/database.hpp"
#include "test_utils.h"

namespace {

using simpledb::test::TempDbFile;

// A snapshot records the last WAL lsn it covers, and reload preserves it.
TEST(StorageTest, SnapshotLsnPreservedAcrossReload) {
  TempDbFile tmp("snapshot_lsn");
  {
    simpledb::Database db(tmp.str());
    db.put("a", "1");
    db.put("b", "2");
  }

  simpledb::Storage storage;
  std::ifstream input(tmp.path(), std::ios::binary);
  storage.load_snapshot(input);
  EXPECT_EQ(storage.last_snapshot_lsn(), uint64_t{2});
}

}  // namespace
