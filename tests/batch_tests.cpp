#include <gtest/gtest.h>

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <optional>
#include <random>
#include <string>
#include <thread>
#include <vector>

#include "simple_db/database.hpp"
#include "simple_db/durable_file.h"
#include "simple_db/logrecord.h"
#include "simple_db/options.h"
#include "simple_db/wal.h"
#include "simple_db/write_batch.h"
#include "test_utils.h"

namespace {

using simpledb::LogRecord;
using simpledb::LogRecordType;
using simpledb::WriteBatch;
using simpledb::test::TempDbFile;

simpledb::Options fast_options() {
  simpledb::Options options;
  options.durability = simpledb::DurabilityMode::kAsync;
  return options;
}

std::string read_file(const std::string& path) {
  std::ifstream input(path, std::ios::binary);
  return std::string(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
}

void write_file(const std::string& path, const std::string& bytes) {
  std::ofstream output(path, std::ios::binary | std::ios::trunc);
  output.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
}

// Records delivered by replay as "type:key=value" strings.
std::vector<std::string> replay_all(simpledb::WAL& wal, bool* clean = nullptr) {
  std::vector<std::string> seen;
  const bool result = wal.replay([&](const LogRecord& record) {
    seen.push_back((record.type() == LogRecordType::Put ? "put:" : "remove:") + record.key() +
                   "=" + record.value());
  });
  if (clean != nullptr) {
    *clean = result;
  }
  return seen;
}

TEST(WriteBatchTest, TracksOperationsAndSizes) {
  WriteBatch batch;
  EXPECT_TRUE(batch.empty());
  batch.put("ab", "123").remove("cd");
  EXPECT_EQ(batch.size(), 2u);
  EXPECT_EQ(batch.byte_size(), 7u);
  batch.clear();
  EXPECT_TRUE(batch.empty());
  EXPECT_EQ(batch.byte_size(), 0u);
}

TEST(WriteBatchTest, AppliesPutsAndRemovesInOrder) {
  TempDbFile tmp("batch_apply");
  simpledb::Database db(tmp.str(), fast_options());
  db.put("old", "x");
  db.put("keep", "k");

  WriteBatch batch;
  batch.put("a", "1").put("b", "2").remove("old").put("a", "3").put("gone", "g").remove("gone");
  db.write(batch);

  EXPECT_EQ(db.get("a"), std::optional<std::string>("3"));
  EXPECT_EQ(db.get("b"), std::optional<std::string>("2"));
  EXPECT_EQ(db.get("old"), std::nullopt);
  EXPECT_EQ(db.get("gone"), std::nullopt);
  EXPECT_EQ(db.get("keep"), std::optional<std::string>("k"));
}

TEST(WriteBatchTest, EmptyBatchIsANoOp) {
  TempDbFile tmp("batch_empty");
  simpledb::Database db(tmp.str(), fast_options());
  db.write(WriteBatch{});
  EXPECT_EQ(db.size(), 0u);
  EXPECT_FALSE(std::filesystem::exists(tmp.wal_path()));
}

TEST(WriteBatchTest, SurvivesCrashWithoutShutdownSnapshot) {
  TempDbFile tmp("batch_crash");
  TempDbFile crashed("batch_crash_copy");
  {
    simpledb::Database db(tmp.str());
    WriteBatch batch;
    batch.put("a", "1").put("b", "2");
    db.write(batch);
    db.put("c", "3");

    // On-disk state as a crash right now would leave it.
    std::filesystem::copy_file(tmp.wal_path(), crashed.wal_path());
  }

  simpledb::Database recovered(crashed.str());
  EXPECT_EQ(recovered.get("a"), std::optional<std::string>("1"));
  EXPECT_EQ(recovered.get("b"), std::optional<std::string>("2"));
  EXPECT_EQ(recovered.get("c"), std::optional<std::string>("3"));
}

TEST(WriteBatchTest, SurvivesCleanReopenAndLaterWrites) {
  TempDbFile tmp("batch_reopen");
  {
    simpledb::Database db(tmp.str(), fast_options());
    WriteBatch batch;
    batch.put("a", "1").put("b", "2");
    db.write(batch);
  }
  {
    simpledb::Database db(tmp.str(), fast_options());
    EXPECT_EQ(db.size(), 2u);
    WriteBatch batch;
    batch.remove("a").put("c", "3");
    db.write(batch);
  }
  simpledb::Database db(tmp.str(), fast_options());
  EXPECT_EQ(db.get("a"), std::nullopt);
  EXPECT_EQ(db.get("b"), std::optional<std::string>("2"));
  EXPECT_EQ(db.get("c"), std::optional<std::string>("3"));
}

TEST(WriteBatchTest, CountsTowardSnapshotThreshold) {
  TempDbFile tmp("batch_threshold");
  simpledb::Options options = fast_options();
  options.snapshot_count_threshold = 3;
  simpledb::Database db(tmp.str(), options);

  WriteBatch batch;
  batch.put("a", "1").put("b", "2").put("c", "3");
  db.write(batch);

  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
  while (!std::filesystem::exists(tmp.path()) && std::chrono::steady_clock::now() < deadline) {
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }
  EXPECT_TRUE(std::filesystem::exists(tmp.path()));
}

TEST(WalBatchTest, AppendBatchAssignsConsecutiveLsnsAndReplaysOperations) {
  TempDbFile tmp("wal_batch");
  simpledb::WAL wal(tmp.wal_path(), simpledb::DurabilityMode::kSync);
  const uint64_t first = wal.append_to_wal(LogRecordType::Put, "base", "0");

  WriteBatch batch;
  batch.put("a", "1").remove("base");
  const uint64_t commit_lsn = wal.append_batch(batch);
  EXPECT_EQ(commit_lsn, first + 4);  // Begin, Put, Remove, Commit
  EXPECT_EQ(wal.current_lsn(), commit_lsn);

  simpledb::WAL reader(tmp.wal_path(), simpledb::DurabilityMode::kSync);
  bool clean = false;
  const auto seen = replay_all(reader, &clean);
  EXPECT_TRUE(clean);
  EXPECT_EQ(seen, (std::vector<std::string>{"put:base=0", "put:a=1", "remove:base="}));
  EXPECT_EQ(reader.current_lsn(), commit_lsn);
}

TEST(WalBatchTest, UncommittedBatchIsNotDelivered) {
  TempDbFile tmp("wal_uncommitted");
  LogRecord(LogRecordType::Put, "base", "0", 1).flush_to_disk(tmp.wal_path(),
                                                              simpledb::DurabilityMode::kAsync);
  LogRecord(LogRecordType::Begin, "", "", 2).flush_to_disk(tmp.wal_path(),
                                                           simpledb::DurabilityMode::kAsync);
  LogRecord(LogRecordType::Put, "a", "1", 3).flush_to_disk(tmp.wal_path(),
                                                           simpledb::DurabilityMode::kAsync);

  simpledb::WAL wal(tmp.wal_path(), simpledb::DurabilityMode::kSync);
  bool clean = true;
  EXPECT_EQ(replay_all(wal, &clean), (std::vector<std::string>{"put:base=0"}));
  EXPECT_FALSE(clean);
  // LSNs of the discarded records are never reused.
  EXPECT_EQ(wal.current_lsn(), 3u);
}

TEST(WalBatchTest, MisplacedFramingStopsReplay) {
  {
    TempDbFile tmp("wal_commit_only");
    LogRecord(LogRecordType::Put, "a", "1", 1).flush_to_disk(tmp.wal_path(),
                                                             simpledb::DurabilityMode::kAsync);
    LogRecord(LogRecordType::Commit, "", "", 2).flush_to_disk(tmp.wal_path(),
                                                              simpledb::DurabilityMode::kAsync);
    LogRecord(LogRecordType::Put, "b", "2", 3).flush_to_disk(tmp.wal_path(),
                                                             simpledb::DurabilityMode::kAsync);
    simpledb::WAL wal(tmp.wal_path(), simpledb::DurabilityMode::kSync);
    bool clean = true;
    EXPECT_EQ(replay_all(wal, &clean), (std::vector<std::string>{"put:a=1"}));
    EXPECT_FALSE(clean);
  }
  {
    TempDbFile tmp("wal_nested_begin");
    LogRecord(LogRecordType::Begin, "", "", 1).flush_to_disk(tmp.wal_path(),
                                                             simpledb::DurabilityMode::kAsync);
    LogRecord(LogRecordType::Put, "a", "1", 2).flush_to_disk(tmp.wal_path(),
                                                             simpledb::DurabilityMode::kAsync);
    LogRecord(LogRecordType::Begin, "", "", 3).flush_to_disk(tmp.wal_path(),
                                                             simpledb::DurabilityMode::kAsync);
    LogRecord(LogRecordType::Commit, "", "", 4).flush_to_disk(tmp.wal_path(),
                                                              simpledb::DurabilityMode::kAsync);
    simpledb::WAL wal(tmp.wal_path(), simpledb::DurabilityMode::kSync);
    bool clean = true;
    EXPECT_TRUE(replay_all(wal, &clean).empty());
    EXPECT_FALSE(clean);
  }
}

TEST(WalBatchTest, CompactKeepsWholeBatchesAfterCutoff) {
  TempDbFile tmp("wal_compact_batch");
  simpledb::WAL wal(tmp.wal_path(), simpledb::DurabilityMode::kAsync);
  WriteBatch first;
  first.put("a", "1").put("b", "2");
  const uint64_t cutoff = wal.append_batch(first);
  WriteBatch second;
  second.put("c", "3").remove("a");
  wal.append_batch(second);

  wal.compact(cutoff);

  simpledb::WAL reader(tmp.wal_path(), simpledb::DurabilityMode::kAsync);
  bool clean = false;
  EXPECT_EQ(replay_all(reader, &clean), (std::vector<std::string>{"put:c=3", "remove:a="}));
  EXPECT_TRUE(clean);
}

TEST(LogRecordTest, RejectsUnknownRecordType) {
  TempDbFile tmp("bad_record_type");
  LogRecord(static_cast<LogRecordType>(99), "k", "v", 1)
      .flush_to_disk(tmp.wal_path(), simpledb::DurabilityMode::kAsync);

  std::ifstream input(tmp.wal_path(), std::ios::binary);
  EXPECT_FALSE(LogRecord::load_from_disk(input).has_value());
}

// Crash-recovery rule: whatever prefix of a batch's bytes reached the disk,
// recovery applies either the whole batch or none of it, and never damages
// earlier committed data.
TEST(BatchCrashTest, EveryTornPrefixOfABatchIsAllOrNothing) {
  TempDbFile source("batch_torn_source");
  TempDbFile target("batch_torn_target");

  {
    simpledb::WAL wal(source.wal_path(), simpledb::DurabilityMode::kAsync);
    wal.append_to_wal(LogRecordType::Put, "base", "0");
  }
  const std::size_t committed_size = std::filesystem::file_size(source.wal_path());
  {
    simpledb::WAL wal(source.wal_path(), simpledb::DurabilityMode::kAsync);
    wal.advance_lsn_to(1);
    WriteBatch batch;
    batch.put("x1", "1").put("x2", "2").remove("base");
    wal.append_batch(batch);
  }
  const std::string full = read_file(source.wal_path());
  ASSERT_GT(full.size(), committed_size);

  const auto reset_target = [&] {
    std::error_code ec;
    std::filesystem::remove(target.path(), ec);
    std::filesystem::remove(target.wal_path(), ec);
    std::filesystem::remove(target.str() + ".tmp", ec);
  };

  for (std::size_t length = committed_size; length <= full.size(); ++length) {
    reset_target();
    write_file(target.wal_path(), full.substr(0, length));
    {
      simpledb::Database db(target.str(), fast_options());
      if (length == full.size()) {
        EXPECT_EQ(db.get("x1"), std::optional<std::string>("1")) << "length " << length;
        EXPECT_EQ(db.get("x2"), std::optional<std::string>("2")) << "length " << length;
        EXPECT_EQ(db.get("base"), std::nullopt) << "length " << length;
      } else {
        EXPECT_EQ(db.get("x1"), std::nullopt) << "length " << length;
        EXPECT_EQ(db.get("x2"), std::nullopt) << "length " << length;
        EXPECT_EQ(db.get("base"), std::optional<std::string>("0")) << "length " << length;
      }
    }
  }
}

// The discarded tail of an incomplete batch must not swallow records written
// after recovery.
TEST(BatchCrashTest, WritesAfterIncompleteBatchSurviveCrash) {
  TempDbFile tmp("batch_incomplete");
  TempDbFile crashed("batch_incomplete_copy");
  LogRecord(LogRecordType::Begin, "", "", 1).flush_to_disk(tmp.wal_path(),
                                                           simpledb::DurabilityMode::kAsync);
  LogRecord(LogRecordType::Put, "lost", "1", 2).flush_to_disk(tmp.wal_path(),
                                                              simpledb::DurabilityMode::kAsync);
  {
    simpledb::Database db(tmp.str());
    EXPECT_EQ(db.get("lost"), std::nullopt);
    db.put("after", "1");
    std::filesystem::copy_file(tmp.path(), crashed.path());
    std::filesystem::copy_file(tmp.wal_path(), crashed.wal_path());
  }

  simpledb::Database recovered(crashed.str());
  EXPECT_EQ(recovered.get("lost"), std::nullopt);
  EXPECT_EQ(recovered.get("after"), std::optional<std::string>("1"));
}

TEST(BatchCrashTest, FailedBatchWriteLeavesNoPartialEffect) {
  TempDbFile tmp("batch_fault");
  {
    simpledb::Database db(tmp.str(), fast_options());
    db.put("a", "1");

    simpledb::set_durable_file_fault_injector(
        [](simpledb::DurableFileOperation operation, const std::string&, std::size_t requested)
            -> std::optional<std::size_t> {
          if (operation == simpledb::DurableFileOperation::kWrite) {
            return requested / 2;  // torn write
          }
          return std::nullopt;
        });
    WriteBatch batch;
    batch.put("x1", "1").put("x2", "2").remove("a");
    EXPECT_THROW(db.write(batch), std::system_error);
    simpledb::clear_durable_file_fault_injector();

    EXPECT_EQ(db.get("a"), std::optional<std::string>("1"));
    EXPECT_EQ(db.get("x1"), std::nullopt);
    EXPECT_EQ(db.get("x2"), std::nullopt);
  }

  simpledb::Database db(tmp.str(), fast_options());
  EXPECT_EQ(db.get("a"), std::optional<std::string>("1"));
  EXPECT_EQ(db.get("x1"), std::nullopt);
  EXPECT_EQ(db.get("x2"), std::nullopt);
}

// Readers must never observe a half-applied batch.
TEST(BatchConcurrencyTest, ReadersNeverSeePartialBatch) {
  TempDbFile tmp("batch_concurrent");
  simpledb::Options options = fast_options();
  options.snapshot_count_threshold = 1000000;
  simpledb::Database db(tmp.str(), options);

  constexpr int kKeys = 8;
  std::atomic<bool> done{false};
  std::atomic<bool> torn{false};

  std::vector<std::thread> readers;
  for (int r = 0; r < 3; ++r) {
    readers.emplace_back([&] {
      while (!done.load()) {
        const auto rows = db.scan_prefix("k");
        if (rows.empty()) {
          continue;
        }
        if (rows.size() != kKeys) {
          torn = true;
        }
        for (const auto& row : rows) {
          if (row.second != rows.front().second) {
            torn = true;
          }
        }
      }
    });
  }

  for (int i = 0; i < 300; ++i) {
    WriteBatch batch;
    for (int k = 0; k < kKeys; ++k) {
      batch.put("k" + std::to_string(k), std::to_string(i));
    }
    db.write(batch);
  }
  done = true;
  for (auto& thread : readers) {
    thread.join();
  }
  EXPECT_FALSE(torn.load());
}

// Randomized comparison against a trivial model, with reopen (recovery)
// checkpoints in between.
TEST(BatchRandomTest, MatchesModelAcrossReopens) {
  TempDbFile tmp("batch_random");
  std::mt19937 rng(12345);
  std::map<std::string, std::string> model;

  const auto random_key = [&] { return "k" + std::to_string(rng() % 12); };

  for (int round = 0; round < 6; ++round) {
    simpledb::Database db(tmp.str(), fast_options());

    std::map<std::string, std::string> actual;
    for (const auto& [key, value] : db.scan()) {
      actual[key] = value;
    }
    ASSERT_EQ(actual, model) << "after reopen in round " << round;

    for (int step = 0; step < 40; ++step) {
      if (rng() % 4 == 0) {
        // Mix in standalone operations.
        const std::string key = random_key();
        db.put(key, "single");
        model[key] = "single";
      }

      WriteBatch batch;
      const int ops = 1 + static_cast<int>(rng() % 5);
      for (int i = 0; i < ops; ++i) {
        const std::string key = random_key();
        if (rng() % 3 == 0) {
          batch.remove(key);
          model.erase(key);
        } else {
          const std::string value = std::to_string(rng());
          batch.put(key, value);
          model[key] = value;
        }
      }
      db.write(batch);

      std::map<std::string, std::string> current;
      for (const auto& [key, value] : db.scan()) {
        current[key] = value;
      }
      ASSERT_EQ(current, model) << "round " << round << " step " << step;
    }
  }
}

}  // namespace
