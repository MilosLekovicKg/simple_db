#pragma once
#include <atomic>
#include <cstdint>
#include <functional>
#include <mutex>
#include <string>
#include "simple_db/logrecord.h"

namespace simpledb {
class WAL {
 public:
  virtual ~WAL() = default;
  WAL(const std::string& wal_file_path) : wal_file_path_(wal_file_path) {}

  // Appends a record and returns the LSN assigned to it.
  virtual uint64_t append_to_wal(const LogRecordType type, const std::string& key, const std::string& value);

  uint64_t current_lsn() const {
    return next_lsn_.load() - 1;
  }

  // Reads every record currently on disk, in order, invoking callback for each.
  void replay(const std::function<void(const LogRecord&)>& callback);

  // Rewrites the WAL file keeping only records with lsn > up_to_lsn.
  void compact(uint64_t up_to_lsn);

 private:
  std::string wal_file_path_;
  std::atomic<uint64_t> next_lsn_{1};
  std::mutex mutex_;
};
}