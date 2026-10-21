#pragma once

#include <cstddef>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "simple_db/durability.h"
#include "simple_db/options.h"
#include "simple_db/snapshot_scheduler.h"
#include "simple_db/wal.h"
#include "simple_db/storage.h"

namespace simpledb {

class Database {
 public:
  Database() = default;
  // Throws std::invalid_argument if options fail Options::validate().
  explicit Database(std::string_view path, Options options = {});
  ~Database();

  void put(std::string_view key, std::string_view value);
  std::optional<std::string> get(std::string_view key) const;
  bool remove(std::string_view key);
  std::vector<std::string> keys() const;
  ScanResults scan(std::optional<std::string_view> start = std::nullopt,
                   std::optional<std::string_view> end = std::nullopt,
                   std::size_t limit = 0) const;
  ScanResults scan_prefix(std::string_view prefix, std::size_t limit = 0) const;
  ScanResults scan_after(
      std::string_view start_after,
      std::optional<std::string_view> end = std::nullopt,
      std::size_t limit = 0) const;
  std::size_t size() const;
  const Options& options() const { return options_; }

 private:
  void load_from_disk();
  void do_snapshot_and_truncate();

  // Serializes put/remove so the WAL append + Storage update happen
  // atomically with respect to other writers. Also held while snapshotting
  // so no write can slip between the snapshot and the WAL compaction.
  std::mutex write_mutex_;

  std::string path_;
  Options options_;
  std::unique_ptr<Storage> storage_;
  std::unique_ptr<WAL> wal_;
  std::unique_ptr<SnapshotScheduler> scheduler_;
};

}  // namespace simpledb
