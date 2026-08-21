#include "simple_db/database.hpp"

#include <algorithm>
#include <fstream>
#include <utility>

namespace simpledb {

Database::Database(std::string_view path) : path_(path) {
  load_from_disk();
}

void Database::put(std::string_view key, std::string_view value) {
  LogRecord record(LogRecordType::Put, key, value);
  record.flush_to_disk(path_);

  store_[std::string(key)] = std::string(value);
}

std::optional<std::string> Database::get(std::string_view key) const {
  auto it = store_.find(std::string(key));
  if (it == store_.end()) {
    return std::nullopt;
  }

  return it->second;
}

bool Database::remove(std::string_view key) {
  LogRecord record(LogRecordType::Remove, key, "");
  record.flush_to_disk(path_);

  return store_.erase(std::string(key)) > 0;
}

std::vector<std::string> Database::keys() const {
  std::vector<std::string> result;
  result.reserve(store_.size());
  for (const auto& [key, _] : store_) {
    result.push_back(key);
  }
  std::sort(result.begin(), result.end());

  return result;
}

std::size_t Database::size() const {
  return store_.size();
}

void Database::load_from_disk() {
  if (path_.empty()) {
    return;
  }

  std::ifstream input(path_, std::ios::binary);
  if (!input.is_open()) {
    return;
  }

  while (input.peek() != std::char_traits<char>::eof()) {
    LogRecord record = LogRecord::load_from_disk(input);
    if (!input) {
      break;
    }

    switch (record.type()) {
      case LogRecordType::Put:
        store_[record.key()] = record.value();
        break;
      case LogRecordType::Remove:
        store_.erase(record.key());
        break;
    }
  }
}

}  // namespace simpledb
