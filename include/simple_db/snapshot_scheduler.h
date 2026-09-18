#pragma once

#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <functional>
#include <mutex>
#include <thread>

namespace simpledb {

// Runs a callback on a background thread whenever enough writes have
// accumulated (count or bytes) or a periodic interval elapses.
class SnapshotScheduler {
 public:
  SnapshotScheduler(std::function<void()> snapshot_fn,
                     std::size_t count_threshold,
                     std::size_t byte_threshold,
                     std::chrono::milliseconds interval);
  ~SnapshotScheduler();

  void start();
  void stop();

  // Called after each write to update the dirty counters that drive triggering.
  void notify_write(std::size_t record_bytes);

 private:
  bool threshold_crossed() const;
  void run();

  std::function<void()> snapshot_fn_;
  std::size_t count_threshold_;
  std::size_t byte_threshold_;
  std::chrono::milliseconds interval_;

  std::thread worker_;
  mutable std::mutex mutex_;
  std::condition_variable cv_;
  bool stop_ = false;
  std::size_t pending_count_ = 0;
  std::size_t pending_bytes_ = 0;
};

}  // namespace simpledb
