#include "simple_db/snapshot_scheduler.h"

namespace simpledb {

SnapshotScheduler::SnapshotScheduler(std::function<void()> snapshot_fn,
                                     std::size_t count_threshold,
                                     std::size_t byte_threshold,
                                     std::chrono::milliseconds interval)
    : snapshot_fn_(std::move(snapshot_fn)),
      count_threshold_(count_threshold),
      byte_threshold_(byte_threshold),
      interval_(interval) {}

SnapshotScheduler::~SnapshotScheduler() {
  stop();
}

void SnapshotScheduler::start() {
  if (worker_.joinable()) {
    return;
  }
  worker_ = std::thread(&SnapshotScheduler::run, this);
}

void SnapshotScheduler::stop() {
  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (stop_) {
      return;
    }
    stop_ = true;
  }
  cv_.notify_all();
  if (worker_.joinable()) {
    worker_.join();
  }
}

void SnapshotScheduler::notify_write(std::size_t record_bytes) {
  bool should_wake = false;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    pending_count_ += 1;
    pending_bytes_ += record_bytes;
    should_wake = threshold_crossed();
  }
  if (should_wake) {
    cv_.notify_one();
  }
}

bool SnapshotScheduler::threshold_crossed() const {
  return pending_count_ >= count_threshold_ || pending_bytes_ >= byte_threshold_;
}

void SnapshotScheduler::run() {
  std::unique_lock<std::mutex> lock(mutex_);
  while (!stop_) {
    cv_.wait_for(lock, interval_, [this] { return stop_ || threshold_crossed(); });
    if (stop_) {
      break;
    }
    if (pending_count_ == 0 && pending_bytes_ == 0) {
      // Woke up on the periodic timeout with nothing dirty; nothing to do.
      continue;
    }

    pending_count_ = 0;
    pending_bytes_ = 0;
    lock.unlock();
    snapshot_fn_();
    lock.lock();
  }
}

}  // namespace simpledb
