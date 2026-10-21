#include "simple_db/storage.h"
#include "simple_db/utils.h"

#include <algorithm>

namespace simpledb {
void Storage::put(std::string_view key, std::string_view value) {
  std::lock_guard<std::mutex> lock(mutex_);
  store_[std::string(key)] = std::string(value);
}

std::optional<std::string> Storage::get(std::string_view key) {
  std::lock_guard<std::mutex> lock(mutex_);
  auto it = store_.find(key);
  if (it != store_.end()) {
    return it->second;
  }

  return std::nullopt;
}

bool Storage::remove(std::string_view key) {
  std::lock_guard<std::mutex> lock(mutex_);
  auto it = store_.find(key);
  if (it == store_.end()) {
    return false;
  }
  store_.erase(it);
  return true;
}

std::vector<std::string> Storage::keys() const {
  std::lock_guard<std::mutex> lock(mutex_);
  std::vector<std::string> result;
  result.reserve(store_.size());
  for (const auto& [key, _] : store_) {
    result.push_back(key);
  }
  return result;
}

ScanResults Storage::scan(std::optional<std::string_view> start,
                          std::optional<std::string_view> end,
                          std::size_t limit) const {
  std::lock_guard<std::mutex> lock(mutex_);
  ScanResults result;
  if (start.has_value() && end.has_value() && *start >= *end) {
    return result;
  }

  auto it = start.has_value() ? store_.lower_bound(*start) : store_.begin();
  while (it != store_.end()) {
    if (end.has_value() && it->first >= *end) {
      break;
    }
    result.emplace_back(it->first, it->second);
    if (limit != 0 && result.size() >= limit) {
      break;
    }
    ++it;
  }
  return result;
}

ScanResults Storage::scan_prefix(std::string_view prefix, std::size_t limit) const {
  std::string end(prefix);
  std::optional<std::string_view> end_bound;
  for (std::size_t i = end.size(); i > 0; --i) {
    if (static_cast<unsigned char>(end[i - 1]) != 0xff) {
      ++end[i - 1];
      end.resize(i);
      end_bound = end;
      break;
    }
  }
  return scan(prefix, end_bound, limit);
}

ScanResults Storage::scan_after(std::string_view start_after,
                                std::optional<std::string_view> end,
                                std::size_t limit) const {
  std::lock_guard<std::mutex> lock(mutex_);
  ScanResults result;
  auto it = store_.upper_bound(start_after);
  while (it != store_.end()) {
    if (end.has_value() && it->first >= *end) {
      break;
    }
    result.emplace_back(it->first, it->second);
    if (limit != 0 && result.size() >= limit) {
      break;
    }
    ++it;
  }
  return result;
}

std::size_t Storage::size() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return store_.size();
}

void Storage::serialize(std::ostream& os) const {
  std::lock_guard<std::mutex> lock(mutex_);
  write_uint64(os, last_lsn_);

  int32_t count = static_cast<int32_t>(store_.size());
  write_int32(os, count);

  for (const auto& [key, value] : store_) {
    write_string(os, key);
    write_string(os, value);
  }
}

void Storage::deserialize(std::istream& is) {
  std::lock_guard<std::mutex> lock(mutex_);
  last_lsn_ = read_uint64(is);

  store_.clear();
  int32_t count = read_int32(is);

  for (int32_t i = 0; i < count; ++i) {
    std::string key = read_string(is);
    std::string value = read_string(is);
    store_[std::move(key)] = std::move(value);
  }
}
}  // namespace simpledb
