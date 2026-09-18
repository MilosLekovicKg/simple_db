#pragma once

#include <cstdint>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>
#include "simple_db/serializable.h"

namespace simpledb {
class Storage : public Serializable {
 public:
  virtual ~Storage() = default;

  virtual void put(std::string_view key, std::string_view value);
  virtual std::optional<std::string> get(std::string_view key);
  virtual bool remove(std::string_view key);
  std::vector<std::string> keys() const;
  std::size_t size() const;

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
  std::unordered_map<std::string, std::string> store_;

  void serialize(std::ostream& os) const override;
  void deserialize(std::istream& is) override;
};
}; // namespace simpledb