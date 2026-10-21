#pragma once

#include <cstdint>
#include <mutex>
#include <optional>
#include <map>
#include <utility>
#include <string>
#include <string_view>
#include <vector>
#include "simple_db/engine.h"
#include "simple_db/serializable.h"

namespace simpledb {
class Storage : public Engine, public Serializable {
 public:
  virtual ~Storage() = default;

  void put(std::string_view key, std::string_view value) override;
  std::optional<std::string> get(std::string_view key) override;
  bool remove(std::string_view key) override;
  std::vector<std::string> keys() const override;
  ScanResults scan(std::optional<std::string_view> start = std::nullopt,
                   std::optional<std::string_view> end = std::nullopt,
                   std::size_t limit = 0) const override;
  ScanResults scan_prefix(std::string_view prefix,
                          std::size_t limit = 0) const override;
  ScanResults scan_after(
      std::string_view start_after,
      std::optional<std::string_view> end = std::nullopt,
      std::size_t limit = 0) const override;
  std::size_t size() const override;

  // Writes a snapshot stamped with the last WAL lsn it covers.
  void snapshot(std::ostream& os, uint64_t last_lsn) const {
    {
      std::lock_guard<std::mutex> lock(mutex_);
      last_lsn_ = last_lsn;
    }
    serialize(os);
  }

  void load_snapshot(std::istream& is) {
    deserialize(is);
  }

  uint64_t last_snapshot_lsn() const {
    return last_lsn_;
  }

 private:
  mutable std::mutex mutex_;
  mutable uint64_t last_lsn_ = 0;
  std::map<std::string, std::string, std::less<>> store_;

  void serialize(std::ostream& os) const override;
  void deserialize(std::istream& is) override;
};
}; // namespace simpledb