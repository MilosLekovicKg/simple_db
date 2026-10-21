#pragma once
#include <atomic>
#include <cstdint>
#include <functional>
#include <mutex>
#include <string>
#include "simple_db/durability.h"
#include "simple_db/logrecord.h"
#include "simple_db/write_batch.h"

namespace simpledb {
class WAL {
 public:
  virtual ~WAL() = default;
  WAL(const std::string& wal_file_path, DurabilityMode durability)
      : wal_file_path_(wal_file_path), durability_(durability) {}

  // Appends a record and returns the LSN assigned to it.
  virtual uint64_t append_to_wal(const LogRecordType type, const std::string& key, const std::string& value);

  // Appends Begin, one record per operation, and Commit with consecutive LSNs
  // in a single write (one fsync in sync mode). Returns the Commit LSN. A crash
  // before the Commit record is durable leaves a batch that replay discards.
  uint64_t append_batch(const WriteBatch& batch);

  uint64_t current_lsn() const {
    return next_lsn_.load() - 1;
  }

  // Ensures future LSNs are greater than `lsn`. Recovery calls this with the
  // snapshot's LSN, because an empty WAL carries no LSN information.
  void advance_lsn_to(uint64_t lsn);

  // Reads the log in order, invoking callback for each Put/Remove record.
  // Operations of a batch are delivered together, in order, only once its
  // Commit record has been read; Begin/Commit records are not delivered.
  // Returns false if it stopped early at a corrupt or torn record, or because
  // the log ends inside an uncommitted batch or has misplaced batch framing;
  // the bytes from the first such problem onward stay in the file until the
  // next compact().
  bool replay(const std::function<void(const LogRecord&)>& callback);

  // Rewrites the WAL file keeping only records with lsn > up_to_lsn. Callers
  // must pass an LSN that does not fall inside a batch (e.g. current_lsn()
  // while writers are excluded), otherwise a batch would be cut in two.
  void compact(uint64_t up_to_lsn);

 private:
  std::string wal_file_path_;
  DurabilityMode durability_;
  std::atomic<uint64_t> next_lsn_{1};
  std::mutex mutex_;
};
}