#include "simple_db/database.hpp"
#include "simple_db/storage.h"

#include <algorithm>
#include <fstream>
#include <utility>

namespace simpledb {

Database::Database(std::string_view path) 
  : path_(path), 
    storage_(std::make_unique<Storage>()),
    wal_(std::make_unique<WAL>(std::string(path_) + ".wal")) {
  load_from_disk();
}

Database::~Database() {
  if (path_.empty()) {
    return;
  }

  std::ofstream output(path_, std::ios::binary);
  if (!output.is_open()) {
    return;
  }

  storage_->snapshot(output);
}

void Database::put(std::string_view key, std::string_view value) {
  wal_->append_to_wal(LogRecordType::Put, std::string(key), std::string(value));
  storage_->put(key, value);
}

std::optional<std::string> Database::get(std::string_view key) const {
  return storage_->get(key);
}

bool Database::remove(std::string_view key) {
  wal_->append_to_wal(LogRecordType::Remove, std::string(key), "");
  return storage_->remove(key);
}

std::vector<std::string> Database::keys() const {
  return storage_->keys();
}

std::size_t Database::size() const {
  return storage_->size();
}

void Database::load_from_disk() {
  if (path_.empty()) {
    return;
  }

  std::ifstream input(path_, std::ios::binary);
  if (!input.is_open()) {
    return;
  }

  storage_->load_snapshot(input);
}

}  // namespace simpledb
