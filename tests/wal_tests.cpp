#include "simple_db/wal.h"

#include <gtest/gtest.h>

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "test_utils.h"

namespace {

using simpledb::test::TempDbFile;

// WAL append_to_wal assigns strictly increasing lsns.
TEST(WalTest, AppendsAssignStrictlyIncreasingLsns) {
  TempDbFile tmp("wal_lsn");
  simpledb::WAL wal(tmp.wal_path());

  const uint64_t lsn1 =
      wal.append_to_wal(simpledb::LogRecordType::Put, "a", "1");
  const uint64_t lsn2 =
      wal.append_to_wal(simpledb::LogRecordType::Put, "b", "2");
  EXPECT_EQ(lsn2, lsn1 + 1);
  EXPECT_EQ(wal.current_lsn(), lsn2);
}

// Corruption detection: a WAL whose last record is damaged must replay
// only the valid prefix and stop gracefully at the first bad record.
TEST(WalTest, CorruptChecksumReplaysValidPrefixOnly) {
  TempDbFile tmp("wal_corruption");
  const std::string wal_path = tmp.wal_path();
  {
    simpledb::WAL wal(wal_path);
    wal.append_to_wal(simpledb::LogRecordType::Put, "a", "1");
    wal.append_to_wal(simpledb::LogRecordType::Put, "b", "2");
    wal.append_to_wal(simpledb::LogRecordType::Put, "c", "3");
  }

  // Flip one byte inside the last record's checksum field.
  const auto size = std::filesystem::file_size(wal_path);
  {
    std::fstream file(wal_path, std::ios::binary | std::ios::in |
                                  std::ios::out);
    file.seekg(static_cast<std::streamoff>(size) - 2);
    char byte{};
    file.get(byte);
    byte = static_cast<char>(byte ^ 0xFF);
    file.seekp(static_cast<std::streamoff>(size) - 2);
    file.put(byte);
  }

  simpledb::WAL wal(wal_path);
  std::vector<std::string> replayed_keys;
  wal.replay([&](const simpledb::LogRecord& record) {
    replayed_keys.push_back(record.key());
  });

  ASSERT_EQ(replayed_keys.size(), size_t{2});
  EXPECT_EQ(replayed_keys[0], "a");
  EXPECT_EQ(replayed_keys[1], "b");
  // The lsn counter must only account for the valid prefix.
  EXPECT_EQ(wal.current_lsn(), uint64_t{2});
}

// Torn write: a WAL truncated mid-record (crash during append) must not
// throw or read garbage - replay yields only the complete records.
TEST(WalTest, TornTailRecordIsIgnoredOnReplay) {
  TempDbFile tmp("wal_torn");
  const std::string wal_path = tmp.wal_path();
  {
    simpledb::WAL wal(wal_path);
    wal.append_to_wal(simpledb::LogRecordType::Put, "a", "1");
    wal.append_to_wal(simpledb::LogRecordType::Put, "b", "2");
  }

  // Drop the tail of the last record, simulating a crash mid-append.
  const auto size = std::filesystem::file_size(wal_path);
  std::filesystem::resize_file(wal_path, size - 3);

  simpledb::WAL wal(wal_path);
  std::vector<std::string> replayed_keys;
  wal.replay([&](const simpledb::LogRecord& record) {
    replayed_keys.push_back(record.key());
  });

  ASSERT_EQ(replayed_keys.size(), size_t{1});
  EXPECT_EQ(replayed_keys[0], "a");
}

}  // namespace
