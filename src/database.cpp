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

  std::ifstream input(path_);
  if (!input.is_open()) {
    return;
  }

  std::string line;
  while (std::getline(input, line)) {
    line = trim_line(std::move(line));
    if (line.empty()) {
      continue;
    }

    const auto delimiter = line.find('\t');
    if (delimiter == std::string::npos) {
      continue;
    }

    std::string key = line.substr(0, delimiter);
    std::string value = line.substr(delimiter + 1);
    store_[std::move(key)] = std::move(value);
  }
}

void Database::flush_to_disk() const {
  if (path_.empty()) {
    return;
  }

  std::ofstream output(path_, std::ios::trunc);
  if (!output.is_open()) {
    return;
  }

  for (const auto& [key, value] : store_) {
    output << key << '\t' << value << '\n';
  }
}

}  // namespace simpledb
