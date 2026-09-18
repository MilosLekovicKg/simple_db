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
  auto it = store_.find(std::string(key));
  if (it != store_.end()) {
    return it->second;
  }

  return std::nullopt;
}

bool Storage::remove(std::string_view key) {
  std::lock_guard<std::mutex> lock(mutex_);
  return store_.erase(std::string(key)) > 0;
}

std::vector<std::string> Storage::keys() const {
  std::lock_guard<std::mutex> lock(mutex_);
  std::vector<std::string> result;
  result.reserve(store_.size());
  for (const auto& [key, _] : store_) {
    result.push_back(key);
  }
  std::sort(result.begin(), result.end());

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
  store_.reserve(count);

  for (int32_t i = 0; i < count; ++i) {
    std::string key = read_string(is);
    std::string value = read_string(is);
    store_[std::move(key)] = std::move(value);
  }
}
}  // namespace simpledb
