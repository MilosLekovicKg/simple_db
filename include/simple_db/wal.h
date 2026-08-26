#pragma once
#include <string>
#include "simple_db/logrecord.h"

namespace simpledb {
class WAL {
 public:
  virtual ~WAL() = default;
  WAL(const std::string& wal_file_path) : wal_file_path_(wal_file_path) {}

  virtual void append_to_wal(const LogRecordType type, const std::string& key, const std::string& value);

 private:
  std::string wal_file_path_;
};
}