#include "simple_db/wal.h"
#include "simple_db/logrecord.h"

namespace simpledb {
void WAL::append_to_wal(LogRecordType type, const std::string& key, const std::string& value) {
  LogRecord record(type, key, value);
  record.flush_to_disk(wal_file_path_);
}
}