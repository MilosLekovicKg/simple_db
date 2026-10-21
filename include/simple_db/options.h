#pragma once

#include <chrono>
#include <cstddef>
#include <stdexcept>

#include "simple_db/durability.h"

namespace simpledb {

// Tunable settings for a Database. Defaults match the values that used to be
// compile-time constants.
struct Options {
  // Durability of WAL appends and snapshot/WAL file replacement.
  DurabilityMode durability = DurabilityMode::kSync;

  // A background snapshot is triggered when either the number of writes or
  // the number of written key+value bytes since the last snapshot reaches its
  // threshold, or when the interval elapses with unsnapshotted writes.
  std::size_t snapshot_count_threshold = 100;
  std::size_t snapshot_byte_threshold = 1 << 20;  // 1 MiB
  std::chrono::milliseconds snapshot_interval{30000};

  // Throws std::invalid_argument if any setting would make the snapshot
  // scheduler misbehave (a zero threshold or interval triggers continuously).
  void validate() const {
    if (snapshot_count_threshold == 0) {
      throw std::invalid_argument("snapshot_count_threshold must be greater than zero");
    }
    if (snapshot_byte_threshold == 0) {
      throw std::invalid_argument("snapshot_byte_threshold must be greater than zero");
    }
    if (snapshot_interval <= std::chrono::milliseconds::zero()) {
      throw std::invalid_argument("snapshot_interval must be greater than zero");
    }
  }
};

}  // namespace simpledb
