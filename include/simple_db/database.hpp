#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>
#include "simple_db/logrecord.h"

namespace simpledb {

class Database {
 public:
  Database() = default;
  explicit Database(std::string_view path);

  void put(std::string_view key, std::string_view value);
  std::optional<std::string> get(std::string_view key) const;
  bool remove(std::string_view key);
  std::vector<std::string> keys() const;
  std::size_t size() const;

 private:
  void load_from_disk();

  std::unordered_map<std::string, std::string> store_;
  std::string path_;
};

}  // namespace simpledb
