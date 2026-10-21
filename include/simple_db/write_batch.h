#pragma once

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace simpledb {

// An ordered list of puts and removes applied as one atomic unit by
// Database::write: after a crash either every operation is recovered or none
// is, and concurrent readers never observe a partial batch. Operations take
// effect in order, so a later operation on a key overrides an earlier one.
class WriteBatch {
 public:
  enum class OpType { kPut, kRemove };

  struct Op {
    OpType type;
    std::string key;
    std::string value;  // empty for kRemove
  };

  WriteBatch& put(std::string_view key, std::string_view value) {
    ops_.push_back({OpType::kPut, std::string(key), std::string(value)});
    byte_size_ += key.size() + value.size();
    return *this;
  }

  WriteBatch& remove(std::string_view key) {
    ops_.push_back({OpType::kRemove, std::string(key), std::string()});
    byte_size_ += key.size();
    return *this;
  }

  void clear() {
    ops_.clear();
    byte_size_ = 0;
  }

  bool empty() const { return ops_.empty(); }
  std::size_t size() const { return ops_.size(); }
  // Total bytes of keys and values in the batch.
  std::size_t byte_size() const { return byte_size_; }
  const std::vector<Op>& ops() const { return ops_; }

 private:
  std::vector<Op> ops_;
  std::size_t byte_size_ = 0;
};

}  // namespace simpledb
