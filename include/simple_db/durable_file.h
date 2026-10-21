#pragma once

#include <cstddef>
#include <functional>
#include <optional>
#include <string>

#include "simple_db/durability.h"

namespace simpledb {

enum class DurableFileOperation {
  kWrite,
  kSync,
};

// Returning a value for kWrite limits the write to that many bytes and lets
// the caller observe a torn write. Returning a value for kSync injects a sync
// failure. Returning nullopt leaves the operation unchanged.
using DurableFileFaultInjector = std::function<std::optional<std::size_t>(
    DurableFileOperation, const std::string&, std::size_t)>;

void set_durable_file_fault_injector(DurableFileFaultInjector injector);
void clear_durable_file_fault_injector();

// Appends bytes to the file at path, creating it if necessary. When mode is
// kSync, flushes and fsyncs before returning; throws std::system_error on
// any I/O failure (open, short write, flush, sync, or close).
void append_file_bytes(const std::string& path, const std::string& bytes, DurabilityMode mode);

void write_file_bytes(const std::string& path, const std::string& bytes, DurabilityMode mode);

// Replaces target with a fully written temporary file and, in sync mode,
// persists the containing directory after the rename.
void replace_file(const std::string& temporary_path, const std::string& target_path,
                  DurabilityMode mode);

}  // namespace simpledb
