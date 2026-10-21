#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <vector>
#include <utility>

namespace simpledb {

using ScanResult = std::pair<std::string, std::string>;
using ScanResults = std::vector<ScanResult>;

class Engine {
 public:
  virtual ~Engine() = default;

  virtual void put(std::string_view key, std::string_view value) = 0;
  virtual std::optional<std::string> get(std::string_view key) = 0;
  virtual bool remove(std::string_view key) = 0;
  virtual std::vector<std::string> keys() const = 0;
  virtual ScanResults scan(
      std::optional<std::string_view> start = std::nullopt,
      std::optional<std::string_view> end = std::nullopt,
      std::size_t limit = 0) const = 0;
  virtual ScanResults scan_prefix(std::string_view prefix,
                                  std::size_t limit = 0) const = 0;
  virtual ScanResults scan_after(
      std::string_view start_after,
      std::optional<std::string_view> end = std::nullopt,
      std::size_t limit = 0) const = 0;
  virtual std::size_t size() const = 0;
};

}  // namespace simpledb
