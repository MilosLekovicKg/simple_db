#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <cstddef>
#include <filesystem>
#include <optional>
#include <string>
#include <system_error>
#include <thread>
#include <vector>

#include "simple_db/database.hpp"
#include "test_utils.h"

namespace {

using simpledb::test::TempDbFile;

// Enough writes should trigger the background scheduler to snapshot and
// compact the WAL without an explicit close.
TEST(ConcurrencyTest, BackgroundSchedulerCompactsWal) {
  TempDbFile tmp("background_snapshot");
  {
    simpledb::Database db(tmp.str());
    for (int i = 0; i < 150; ++i) {
      db.put("key" + std::to_string(i), "value" + std::to_string(i));
    }

    bool compacted = false;
    for (int attempt = 0; attempt < 50 && !compacted; ++attempt) {
      std::this_thread::sleep_for(std::chrono::milliseconds(100));
      std::error_code ec;
      const auto wal_size = std::filesystem::file_size(tmp.wal_path(), ec);
      // Uncompacted, 150 records would take several KB; a triggered
      // compaction drops all but the handful of records written after it.
      if (!ec && wal_size < 3000) {
        compacted = true;
      }
    }
    EXPECT_TRUE(compacted);
  }
}

// Concurrent writers must not interleave the WAL append and the Storage
// update: every put/remove is atomic with respect to other writers, so
// the final state and the post-reload (replayed) state must agree.
TEST(ConcurrencyTest, ConcurrentWritesSurviveReload) {
  TempDbFile tmp("concurrent_writes");
  constexpr int kThreads = 4;
  constexpr int kWritesPerThread = 100;
  {
    simpledb::Database db(tmp.str());

    std::vector<std::thread> threads;
    threads.reserve(kThreads);
    for (int t = 0; t < kThreads; ++t) {
      threads.emplace_back([&db, t] {
        for (int i = 0; i < kWritesPerThread; ++i) {
          const std::string key = "key" + std::to_string(i);
          const std::string value =
              "t" + std::to_string(t) + "_v" + std::to_string(i);
          db.put(key, value);
          if (i % 3 == 0) {
            db.remove(key);
            db.put(key, value);
          }
        }
      });
    }
    for (auto& thread : threads) {
      thread.join();
    }

    EXPECT_EQ(db.size(), static_cast<size_t>(kWritesPerThread));
  }

  {
    // Recovery must converge to a consistent state: no torn or reordered
    // WAL/Storage pairs from the concurrent phase above.
    simpledb::Database db(tmp.str());
    ASSERT_EQ(db.size(), static_cast<size_t>(kWritesPerThread));
    for (int i = 0; i < kWritesPerThread; ++i) {
      EXPECT_TRUE(db.get("key" + std::to_string(i)).has_value());
    }
  }
}

// Stress test with correctness verification. Three mechanisms work
// together to catch race-condition corruption instead of just crashes:
//
// 1. Writers use disjoint key spaces, so the final value of every key
//    is deterministic (the last write in the owning thread's sequence).
// 2. Every value a key can ever hold is a pure function of
//    (writer, op index), so readers can reject any observed value that
//    is not one of the legal ones - that catches torn reads of
//    std::string payloads caused by a missing/short-lived lock.
// 3. After the threads join, the full in-memory state must match the
//    expected final state exactly, and a fresh reload (snapshot + WAL
//    replay) must converge to the same state.
//
// Note: gtest assertions must not run inside worker threads, so readers
// only count anomalies and the main thread asserts after join.
TEST(ConcurrencyTest, StressConvergesToDeterministicState) {
  TempDbFile tmp("concurrent_stress");
  constexpr int kReaderThreads = 2;
  constexpr int kWriterThreads = 3;
  constexpr int kOpsPerWriter = 300;
  constexpr int kReadsPerReader = 500;

  const auto key_for = [](int w, int i) {
    return "w" + std::to_string(w) + "_key_" + std::to_string(i);
  };
  const auto value_for = [](int w, int i) {
    return "w" + std::to_string(w) + "_v" + std::to_string(i);
  };
  // The last write for key (w, i) is value_for(w, i) + "_upd" when i is
  // even, value_for(w, i) otherwise - no other thread ever touches it.
  const auto expected_final = [&value_for](int w, int i) {
    return (i % 2 == 0) ? value_for(w, i) + "_upd" : value_for(w, i);
  };

  std::atomic<int> corrupted_reads{0};
  {
    simpledb::Database db(tmp.str());

    std::vector<std::thread> threads;
    threads.reserve(kReaderThreads + kWriterThreads);

    // Writers: each owns a disjoint key space.
    for (int w = 0; w < kWriterThreads; ++w) {
      threads.emplace_back([&db, w, &key_for, &value_for] {
        for (int i = 0; i < kOpsPerWriter; ++i) {
          db.put(key_for(w, i), value_for(w, i));
          if (i % 2 == 0) {
            db.put(key_for(w, i), value_for(w, i) + "_upd");
          }
        }
      });
    }

    // Readers: for every probed key the only legal outcomes are
    // "not present yet" or one of the two values the owning writer ever
    // stores. Anything else (garbage bytes, a torn string, another
    // writer's value) means Storage was read while being mutated.
    for (int r = 0; r < kReaderThreads; ++r) {
      threads.emplace_back([&db, &key_for, &value_for, &corrupted_reads] {
        for (int i = 0; i < kReadsPerReader; ++i) {
          for (int w = 0; w < kWriterThreads; ++w) {
            const int key_idx = (i + w) % kOpsPerWriter;
            const auto val = db.get(key_for(w, key_idx));
            if (val.has_value()) {
              const std::string base = value_for(w, key_idx);
              if (*val != base && *val != base + "_upd") {
                ++corrupted_reads;
              }
            }
          }
        }
      });
    }

    for (auto& thread : threads) {
      thread.join();
    }

    // No reader may ever observe an impossible value.
    EXPECT_EQ(corrupted_reads.load(), 0);

    // The final in-memory state must match the deterministic expectation.
    ASSERT_EQ(db.size(),
              static_cast<size_t>(kWriterThreads * kOpsPerWriter));
    for (int w = 0; w < kWriterThreads; ++w) {
      for (int i = 0; i < kOpsPerWriter; ++i) {
        EXPECT_EQ(db.get(key_for(w, i)),
                  std::optional<std::string>(expected_final(w, i)));
      }
    }
  }

  {
    // Reload from snapshot + WAL: recovery must converge to the exact
    // same state, proving no WAL/Storage pair was torn or reordered
    // during the concurrent phase above.
    simpledb::Database db(tmp.str());
    ASSERT_EQ(db.size(),
              static_cast<size_t>(kWriterThreads * kOpsPerWriter));
    for (int w = 0; w < kWriterThreads; ++w) {
      for (int i = 0; i < kOpsPerWriter; ++i) {
        EXPECT_EQ(db.get(key_for(w, i)),
                  std::optional<std::string>(expected_final(w, i)));
      }
    }
  }
}

}  // namespace
