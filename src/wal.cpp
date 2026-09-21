#include "simple_db/wal.h"
#include "simple_db/logrecord.h"

#include <algorithm>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <stdexcept>

namespace simpledb {
uint64_t WAL::append_to_wal(LogRecordType type, const std::string& key, const std::string& value) {
  std::lock_guard<std::mutex> lock(mutex_);
  const uint64_t lsn = next_lsn_.fetch_add(1);
  LogRecord record(type, key, value, lsn);
  record.flush_to_disk(wal_file_path_);
  return lsn;
}

void WAL::replay(const std::function<void(const LogRecord&)>& callback) {
  std::lock_guard<std::mutex> lock(mutex_);
  std::ifstream input(wal_file_path_, std::ios::binary);
  if (!input.is_open()) {
    return;
  }

  uint64_t max_lsn = next_lsn_.load() - 1;
  while (input.peek() != std::ifstream::traits_type::eof()) {
    const auto record = LogRecord::load_from_disk(input);
    if (!record.has_value()) {
      // Corrupt or torn record: trust only the valid prefix and stop here.
      std::cerr << "WAL replay: ignoring " << wal_file_path_
                << " from the first corrupt record onward\n";
      break;
    }
    callback(*record);
    max_lsn = std::max(max_lsn, record->lsn());
  }

  next_lsn_.store(max_lsn + 1);
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
      record->flush_to_disk(tmp_path);
    }
  }

  input.close();
  std::remove(wal_file_path_.c_str());
  std::rename(tmp_path.c_str(), wal_file_path_.c_str());
}
}
