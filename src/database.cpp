#include "simple_db/database.hpp"

#include <algorithm>
#include <fstream>
#include <utility>

namespace simpledb {

namespace {

std::string trim_line(std::string line) {
  line.erase(line.find_last_not_of("\r\n") + 1);
  return line;
}

}  // namespace

Database::Database(std::string_view path) : path_(path) {
  load_from_disk();
}

void Database::put(std::string_view key, std::string_view value) {
  store_[std::string(key)] = std::string(value);
  flush_to_disk();
}

std::optional<std::string> Database::get(std::string_view key) const {
  auto it = store_.find(std::string(key));
  if (it == store_.end()) {
    return std::nullopt;
  }
  return it->second;
}

bool Database::remove(std::string_view key) {
  const bool removed = store_.erase(std::string(key)) > 0;
  if (removed) {
    flush_to_disk();
  }
  return removed;
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

void Database::clear() {
  store_.clear();
  flush_to_disk();
}

void Database::load_from_disk() {
  if (path_.empty()) {
    return;
  }

  std::ifstream input(path_, std::ios::binary);
  if (!input.is_open()) {
    return;
  }

  int count{};
  input.read(reinterpret_cast<char*>(&count), sizeof(count));

  for (int i = 0; i < count; ++i) {
    int key_size{};
    input.read(reinterpret_cast<char*>(&key_size), sizeof(key_size));
    std::string key(key_size, '\0');
    input.read(&key[0], key_size);

    int value_size{};
    input.read(reinterpret_cast<char*>(&value_size), sizeof(value_size));
    std::string value(value_size, '\0');
    input.read(&value[0], value_size);

    store_[std::move(key)] = std::move(value);
  }
}

void Database::flush_to_disk() const {
  if (path_.empty()) {
    return;
  }

  std::ofstream output(path_, std::ios::binary | std::ios::trunc);
  if (!output.is_open()) {
    return;
  }

  const std::int32_t count = static_cast<std::int32_t>(store_.size());
  output.write(reinterpret_cast<const char*>(&count), sizeof(count));

  for (const auto& [key, value] : store_) {
    const std::int32_t key_size = static_cast<std::int32_t>(key.size());
    output.write(reinterpret_cast<const char*>(&key_size), sizeof(key_size));
    output.write(key.data(), key_size);

    const std::int32_t value_size = static_cast<std::int32_t>(value.size());
    output.write(reinterpret_cast<const char*>(&value_size), sizeof(value_size));
    output.write(value.data(), value_size);
  }
}

}  // namespace simpledb
