#include "simple_db/wal.h"
#include "simple_db/durable_file.h"
#include "simple_db/logrecord.h"

#include <algorithm>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <vector>

namespace simpledb {
uint64_t WAL::append_to_wal(LogRecordType type, const std::string& key, const std::string& value) {
  std::lock_guard<std::mutex> lock(mutex_);
  const uint64_t lsn = next_lsn_.fetch_add(1);
  LogRecord record(type, key, value, lsn);
  record.flush_to_disk(wal_file_path_, durability_);
  return lsn;
}

uint64_t WAL::append_batch(const WriteBatch& batch) {
  std::lock_guard<std::mutex> lock(mutex_);
  const std::size_t record_count = batch.size() + 2;
  uint64_t lsn = next_lsn_.fetch_add(record_count);

  std::string bytes = LogRecord(LogRecordType::Begin, "", "", lsn++).to_bytes();
  for (const auto& op : batch.ops()) {
    const LogRecordType type =
        op.type == WriteBatch::OpType::kPut ? LogRecordType::Put : LogRecordType::Remove;
    bytes += LogRecord(type, op.key, op.value, lsn++).to_bytes();
  }
  const uint64_t commit_lsn = lsn;
  bytes += LogRecord(LogRecordType::Commit, "", "", commit_lsn).to_bytes();

  append_file_bytes(wal_file_path_, bytes, durability_);
  return commit_lsn;
}

void WAL::advance_lsn_to(uint64_t lsn) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (next_lsn_.load() <= lsn) {
    next_lsn_.store(lsn + 1);
  }
}

bool WAL::replay(const std::function<void(const LogRecord&)>& callback) {
  std::lock_guard<std::mutex> lock(mutex_);
  std::ifstream input(wal_file_path_, std::ios::binary);
  if (!input.is_open()) {
    return true;
  }

  bool clean = true;
  uint64_t max_lsn = next_lsn_.load() - 1;
  // Operations of the batch currently being read; delivered at its Commit.
  std::optional<std::vector<LogRecord>> pending;
  while (input.peek() != std::ifstream::traits_type::eof()) {
    const auto record = LogRecord::load_from_disk(input);
    if (!record.has_value()) {
      // Corrupt or torn record: trust only the valid prefix and stop here.
      std::cerr << "WAL replay: ignoring " << wal_file_path_
                << " from the first corrupt record onward\n";
      clean = false;
      break;
    }
    max_lsn = std::max(max_lsn, record->lsn());

    bool framing_ok = true;
    switch (record->type()) {
      case LogRecordType::Begin:
        framing_ok = !pending.has_value();
        if (framing_ok) {
          pending.emplace();
        }
        break;
      case LogRecordType::Commit:
        framing_ok = pending.has_value();
        if (framing_ok) {
          for (const auto& op : *pending) {
            callback(op);
          }
          pending.reset();
        }
        break;
      case LogRecordType::Put:
      case LogRecordType::Remove:
        if (pending.has_value()) {
          pending->push_back(*record);
        } else {
          callback(*record);
        }
        break;
    }
    if (!framing_ok) {
      std::cerr << "WAL replay: unexpected batch framing in " << wal_file_path_
                << "; ignoring the log from this record onward\n";
      clean = false;
      pending.reset();
      break;
    }
  }

  if (pending.has_value()) {
    // The log ends inside a batch whose Commit never became durable.
    std::cerr << "WAL replay: discarding uncommitted batch at the end of " << wal_file_path_
              << "\n";
    clean = false;
  }

  next_lsn_.store(max_lsn + 1);
  return clean;
}

void WAL::compact(uint64_t up_to_lsn) {
  std::lock_guard<std::mutex> lock(mutex_);
  std::ifstream input(wal_file_path_, std::ios::binary);
  if (!input.is_open()) {
    return;
  }

  const std::string tmp_path = wal_file_path_ + ".tmp";
  {
    std::ofstream truncate(tmp_path, std::ios::binary | std::ios::trunc);
    if (!truncate.is_open()) {
      throw std::runtime_error("Failed to open temp WAL file for writing: " + tmp_path);
    }
  }

  while (input.peek() != std::ifstream::traits_type::eof()) {
    const auto record = LogRecord::load_from_disk(input);
    if (!record.has_value()) {
      // Corrupt or torn record: keep only the valid prefix and stop here.
      std::cerr << "WAL compaction: ignoring " << wal_file_path_
                << " from the first corrupt record onward\n";
      break;
    }
    if (record->lsn() > up_to_lsn) {
      // Compaction just copies already-durable records into a fresh file;
      // fsyncing each one here would be one syscall per surviving record.
      record->flush_to_disk(tmp_path, DurabilityMode::kAsync);
    }
  }

  input.close();
  replace_file(tmp_path, wal_file_path_, durability_);
}
}
