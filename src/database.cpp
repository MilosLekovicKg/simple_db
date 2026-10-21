#include "simple_db/database.hpp"
#include "simple_db/durable_file.h"
#include "simple_db/storage.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <utility>

namespace simpledb {

namespace {
constexpr std::size_t kSnapshotCountThreshold = 100;
constexpr std::size_t kSnapshotByteThreshold = 1 << 20;  // 1 MiB
constexpr std::chrono::milliseconds kSnapshotInterval{30000};
}  // namespace

Database::Database(std::string_view path, DurabilityMode durability)
  : path_(path), 
    durability_(durability),
    storage_(std::make_unique<Storage>()),
    wal_(std::make_unique<WAL>(std::string(path_) + ".wal", durability)) {
  load_from_disk();

  scheduler_ = std::make_unique<SnapshotScheduler>(
      [this] { do_snapshot_and_truncate(); },
      kSnapshotCountThreshold, kSnapshotByteThreshold, kSnapshotInterval);
  scheduler_->start();
}

Database::~Database() {
  if (scheduler_) {
    scheduler_->stop();
  }

  if (path_.empty()) {
    return;
  }

  do_snapshot_and_truncate();
}

void Database::put(std::string_view key, std::string_view value) {
  std::lock_guard<std::mutex> lock(write_mutex_);
  wal_->append_to_wal(LogRecordType::Put, std::string(key), std::string(value));
  storage_->put(key, value);
  if (scheduler_) {
    scheduler_->notify_write(key.size() + value.size());
  }
}

std::optional<std::string> Database::get(std::string_view key) const {
  return storage_->get(key);
}

bool Database::remove(std::string_view key) {
  std::lock_guard<std::mutex> lock(write_mutex_);
  wal_->append_to_wal(LogRecordType::Remove, std::string(key), "");
  bool removed = storage_->remove(key);
  if (scheduler_) {
    scheduler_->notify_write(key.size());
  }
  return removed;
}

std::vector<std::string> Database::keys() const {
  return storage_->keys();
}

ScanResults Database::scan(std::optional<std::string_view> start,
                           std::optional<std::string_view> end,
                           std::size_t limit) const {
  return storage_->scan(start, end, limit);
}

ScanResults Database::scan_prefix(std::string_view prefix, std::size_t limit) const {
  return storage_->scan_prefix(prefix, limit);
}

ScanResults Database::scan_after(std::string_view start_after,
                                 std::optional<std::string_view> end,
                                 std::size_t limit) const {
  return storage_->scan_after(start_after, end, limit);
}

std::size_t Database::size() const {
  return storage_->size();
}

void Database::load_from_disk() {
  if (path_.empty()) {
    return;
  }

  {
    std::ifstream input(path_, std::ios::binary);
    if (input.is_open()) {
      storage_->load_snapshot(input);
    }
  }

  const uint64_t last_snapshot_lsn = storage_->last_snapshot_lsn();
  bool replayed_any = false;
  wal_->replay([&](const LogRecord& record) {
    if (record.lsn() <= last_snapshot_lsn) {
      return;
    }
    if (record.type() == LogRecordType::Put) {
      storage_->put(record.key(), record.value());
    } else {
      storage_->remove(record.key());
    }
    replayed_any = true;
  });

  if (replayed_any) {
    do_snapshot_and_truncate();
  }
}

void Database::do_snapshot_and_truncate() {
  if (path_.empty()) {
    return;
  }

  std::lock_guard<std::mutex> lock(write_mutex_);
  const uint64_t lsn = wal_->current_lsn();
  const std::string tmp_path = path_ + ".tmp";
  std::ostringstream snapshot;
  storage_->snapshot(snapshot, lsn);
  const std::string snapshot_bytes = snapshot.str();
  write_file_bytes(tmp_path, snapshot_bytes, durability_);
  replace_file(tmp_path, path_, durability_);

  wal_->compact(lsn);
}

}  // namespace simpledb
