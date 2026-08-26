#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "simple_db/wal.h"
#include "simple_db/storage.h"

namespace simpledb {

class Database {
 public:
  Database() = default;
  explicit Database(std::string_view path);
  ~Database();

  void put(std::string_view key, std::string_view value);
  std::optional<std::string> get(std::string_view key) const;
  bool remove(std::string_view key);
  std::vector<std::string> keys() const;
  std::size_t size() const;

 private:
  void load_from_disk();

  std::string path_;
  std::unique_ptr<Storage> storage_;
  std::unique_ptr<WAL> wal_;
};

}  // namespace simpledb
