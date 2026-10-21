#pragma once

#include <optional>
#include <string_view>

namespace simpledb {

// kAsync: bytes are written but may still sit in OS buffers; a crash can
// lose them. kSync: the write is flushed and fsynced before returning.
enum class DurabilityMode {
  kAsync,
  kSync,
};

inline std::string_view to_string(DurabilityMode mode) {
  switch (mode) {
    case DurabilityMode::kAsync:
      return "async";
    case DurabilityMode::kSync:
      return "sync";
  }
}

inline std::optional<DurabilityMode> parse_durability_mode(std::string_view text) {
  if (text == "sync") {
    return DurabilityMode::kSync;
  }
  if (text == "async") {
    return DurabilityMode::kAsync;
  }
  return std::nullopt;
}

}  // namespace simpledb
