#include "simple_db/database.hpp"
#include "simple_db/logrecord.h"
#include "simple_db/storage.h"
#include "simple_db/wal.h"

#include <atomic>
#include <cassert>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>
#include <thread>
#include <vector>

namespace {

std::filesystem::path test_path(const std::string& name) {
  return std::filesystem::temp_directory_path() / ("simple_db_" + name + ".db");
}

void remove_test_file(const std::filesystem::path& path) {
  std::filesystem::remove(path);
  std::filesystem::remove(path.string() + ".wal");
  std::filesystem::remove(path.string() + ".tmp");
}

}  // namespace

int main() {
  {
    const auto path = test_path("missing_key");
    remove_test_file(path);
    simpledb::Database db(path.string());

    assert(!db.get("unknown").has_value());
    assert(!db.remove("unknown"));
    assert(db.size() == 0);
    assert(db.keys().empty());

    remove_test_file(path);
  }

  {
    const auto path = test_path("crud");
    remove_test_file(path);
    simpledb::Database db(path.string());

    db.put("name", "Ada");
    assert(db.get("name") == std::optional<std::string>("Ada"));
    assert(db.remove("name"));
    assert(!db.get("name").has_value());
    assert(!db.remove("name"));

    remove_test_file(path);
  }

  {
    const auto path = test_path("overwrite");
    remove_test_file(path);
    {
      simpledb::Database db(path.string());
      db.put("name", "Ada");
      db.put("name", "Ada Lovelace");
      assert(db.get("name") == std::optional<std::string>("Ada Lovelace"));
      assert(db.size() == 1);
    }

    {
      simpledb::Database db(path.string());
      assert(db.get("name") == std::optional<std::string>("Ada Lovelace"));
      assert(db.size() == 1);
    }

    remove_test_file(path);
  }

  {
    const auto path = test_path("operation_order");
    remove_test_file(path);
    {
      simpledb::Database db(path.string());
      db.put("x", "1");
      db.put("x", "2");
      assert(db.remove("x"));
      db.put("x", "3");
    }

    {
      simpledb::Database db(path.string());
      assert(db.get("x") == std::optional<std::string>("3"));
    }

    remove_test_file(path);
  }

  {
    const auto path = test_path("keys");
    remove_test_file(path);
    simpledb::Database db(path.string());
    db.put("name", "Ada");
    db.put("language", "C++");
    db.put("year", "1842");

    const auto keys = db.keys();
    assert(keys.size() == 3);
    assert(keys[0] == "language");
    assert(keys[1] == "name");
    assert(keys[2] == "year");

    const auto size = db.size();
    assert(size == 3);

    remove_test_file(path);
  }

  {
    const auto path = test_path("empty_and_special_values");
    remove_test_file(path);
    {
      simpledb::Database db(path.string());
      db.put("", "empty key");
      db.put("empty value", "");
      db.put("special", std::string("line\nwith\0null", 14));

      assert(db.get("") == std::optional<std::string>("empty key"));
      assert(db.get("empty value") == std::optional<std::string>(""));
      assert(db.get("special") ==
             std::optional<std::string>(std::string("line\nwith\0null", 14)));
    }

    {
      simpledb::Database db(path.string());
      assert(db.get("") == std::optional<std::string>("empty key"));
      assert(db.get("empty value") == std::optional<std::string>(""));
      assert(db.get("special") ==
             std::optional<std::string>(std::string("line\nwith\0null", 14)));
    }

    remove_test_file(path);
  }

  {
    const auto path = test_path("logrecord");
    remove_test_file(path);
    simpledb::LogRecord put_record(simpledb::LogRecordType::Put, "key", "value");
    simpledb::LogRecord remove_record(simpledb::LogRecordType::Remove, "key", "");
    put_record.flush_to_disk(path.string());
    remove_record.flush_to_disk(path.string());

    {
      std::ifstream input(path, std::ios::binary);
      const auto loaded_put = simpledb::LogRecord::load_from_disk(input);
      const auto loaded_remove = simpledb::LogRecord::load_from_disk(input);
      assert(loaded_put.has_value());
      assert(loaded_put->type() == simpledb::LogRecordType::Put);
      assert(loaded_put->key() == "key");
      assert(loaded_put->value() == "value");
      assert(loaded_remove.has_value());
      assert(loaded_remove->type() == simpledb::LogRecordType::Remove);
      assert(loaded_remove->key() == "key");
      assert(loaded_remove->value().empty());
    }

    remove_test_file(path);
  }

  {
    // WAL append_to_wal assigns strictly increasing lsns.
    const auto path = test_path("wal_lsn");
    remove_test_file(path);
    const std::string wal_path = path.string() + ".wal";
    simpledb::WAL wal(wal_path);
    const uint64_t lsn1 = wal.append_to_wal(simpledb::LogRecordType::Put, "a", "1");
    const uint64_t lsn2 = wal.append_to_wal(simpledb::LogRecordType::Put, "b", "2");
    assert(lsn2 == lsn1 + 1);
    assert(wal.current_lsn() == lsn2);

    remove_test_file(path);
  }

  {
    // A snapshot records the last WAL lsn it covers, and reload preserves it.
    const auto path = test_path("snapshot_lsn");
    remove_test_file(path);
    {
      simpledb::Database db(path.string());
      db.put("a", "1");
      db.put("b", "2");
    }

    {
      simpledb::Storage storage;
      std::ifstream input(path, std::ios::binary);
      storage.load_snapshot(input);
      assert(storage.last_snapshot_lsn() == 2);
    }

    remove_test_file(path);
  }

  {
    // Records appended to the WAL beyond the snapshot's lsn are replayed on load.
    const auto path = test_path("wal_replay");
    remove_test_file(path);
    const std::string wal_path = path.string() + ".wal";
    {
      simpledb::Database db(path.string());
      db.put("a", "1");

      // Simulate a write that reached the WAL but whose snapshot was never
      // taken (e.g. a crash right after append_to_wal), by appending a
      // record with an lsn beyond what this Database instance has committed.
      simpledb::LogRecord uncommitted(simpledb::LogRecordType::Put, "b", "2", 999);
      uncommitted.flush_to_disk(wal_path);
    }

    {
      simpledb::Database db(path.string());
      assert(db.get("a") == std::optional<std::string>("1"));
      assert(db.get("b") == std::optional<std::string>("2"));
    }

    remove_test_file(path);
  }

  {
    // Enough writes should trigger the background scheduler to snapshot and
    // compact the WAL without an explicit close.
    const auto path = test_path("background_snapshot");
    remove_test_file(path);
    const std::string wal_path = path.string() + ".wal";
    {
      simpledb::Database db(path.string());
      for (int i = 0; i < 150; ++i) {
        db.put("key" + std::to_string(i), "value" + std::to_string(i));
      }

      bool compacted = false;
      for (int attempt = 0; attempt < 50 && !compacted; ++attempt) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        std::error_code ec;
        const auto wal_size = std::filesystem::file_size(wal_path, ec);
        // Uncompacted, 150 records would take several KB; a triggered
        // compaction drops all but the handful of records written after it.
        if (!ec && wal_size < 3000) {
          compacted = true;
        }
      }
      assert(compacted);
    }

    remove_test_file(path);
  }

  {
    // Concurrent writers must not interleave the WAL append and the Storage
    // update: every put/remove is atomic with respect to other writers, so
    // the final state and the post-reload (replayed) state must agree.
    const auto path = test_path("concurrent_writes");
    remove_test_file(path);
    constexpr int kThreads = 4;
    constexpr int kWritesPerThread = 100;
    {
      simpledb::Database db(path.string());

      std::vector<std::thread> threads;
      threads.reserve(kThreads);
      for (int t = 0; t < kThreads; ++t) {
        threads.emplace_back([&db, t] {
          for (int i = 0; i < kWritesPerThread; ++i) {
            const std::string key = "key" + std::to_string(i);
            const std::string value = "t" + std::to_string(t) + "_v" + std::to_string(i);
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

      assert(db.size() == kWritesPerThread);
    }

    {
      // Recovery must converge to a consistent state: no torn or reordered
      // WAL/Storage pairs from the concurrent phase above.
      simpledb::Database db(path.string());
      assert(db.size() == kWritesPerThread);
      for (int i = 0; i < kWritesPerThread; ++i) {
        assert(db.get("key" + std::to_string(i)).has_value());
      }
    }

    remove_test_file(path);
  }

  {
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
    const auto path = test_path("concurrent_stress");
    remove_test_file(path);
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
      simpledb::Database db(path.string());

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
      assert(corrupted_reads == 0);

      // The final in-memory state must match the deterministic expectation.
      assert(db.size() == kWriterThreads * kOpsPerWriter);
      for (int w = 0; w < kWriterThreads; ++w) {
        for (int i = 0; i < kOpsPerWriter; ++i) {
          assert(db.get(key_for(w, i)) ==
                 std::optional<std::string>(expected_final(w, i)));
        }
      }
    }

    {
      // Reload from snapshot + WAL: recovery must converge to the exact
      // same state, proving no WAL/Storage pair was torn or reordered
      // during the concurrent phase above.
      simpledb::Database db(path.string());
      assert(db.size() == kWriterThreads * kOpsPerWriter);
      for (int w = 0; w < kWriterThreads; ++w) {
        for (int i = 0; i < kOpsPerWriter; ++i) {
          assert(db.get(key_for(w, i)) ==
                 std::optional<std::string>(expected_final(w, i)));
        }
      }
    }

    remove_test_file(path);
  }

  {
    // Corruption detection: a WAL whose last record is damaged must replay
    // only the valid prefix and stop gracefully at the first bad record.
    const auto path = test_path("wal_corruption");
    remove_test_file(path);
    const std::string wal_path = path.string() + ".wal";
    {
      simpledb::WAL wal(wal_path);
      wal.append_to_wal(simpledb::LogRecordType::Put, "a", "1");
      wal.append_to_wal(simpledb::LogRecordType::Put, "b", "2");
      wal.append_to_wal(simpledb::LogRecordType::Put, "c", "3");
    }

    // Flip one byte inside the last record's checksum field.
    const auto size = std::filesystem::file_size(wal_path);
    {
      std::fstream file(wal_path, std::ios::binary | std::ios::in | std::ios::out);
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
    assert(replayed_keys.size() == 2);
    assert(replayed_keys[0] == "a");
    assert(replayed_keys[1] == "b");
    // The lsn counter must only account for the valid prefix.
    assert(wal.current_lsn() == 2);

    remove_test_file(path);
  }

  {
    // Torn write: a WAL truncated mid-record (crash during append) must not
    // throw or read garbage - replay yields only the complete records.
    const auto path = test_path("wal_torn");
    remove_test_file(path);
    const std::string wal_path = path.string() + ".wal";
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
    assert(replayed_keys.size() == 1);
    assert(replayed_keys[0] == "a");

    remove_test_file(path);
  }

  return 0;
}
