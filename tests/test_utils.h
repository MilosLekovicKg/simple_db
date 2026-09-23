#pragma once

#include <filesystem>
#include <string>
#include <system_error>

namespace simpledb::test {

// RAII guard for a temporary database file and its sidecars (.wal, .tmp).
// Removes them on construction and destruction so failed EXPECT/ASSERT
// checks cannot leak state into later runs (leftover .wal files would
// silently be replayed by the next run).
class TempDbFile {
 public:
  explicit TempDbFile(const std::string& name)
      : path_(std::filesystem::temp_directory_path() /
              ("simple_db_" + name + ".db")) {
    Cleanup();
  }

  ~TempDbFile() { Cleanup(); }

  TempDbFile(const TempDbFile&) = delete;
  TempDbFile& operator=(const TempDbFile&) = delete;

  const std::filesystem::path& path() const { return path_; }
  std::string str() const { return path_.string(); }
  std::string wal_path() const { return path_.string() + ".wal"; }

 private:
  void Cleanup() {
    std::error_code ec;
    std::filesystem::remove(path_, ec);
    std::filesystem::remove(path_.string() + ".wal", ec);
    std::filesystem::remove(path_.string() + ".tmp", ec);
  }

  std::filesystem::path path_;
};

}  // namespace simpledb::test
