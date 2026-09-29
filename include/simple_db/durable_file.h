#pragma once

#include <string>

#include "simple_db/durability.h"

namespace simpledb {

// Appends bytes to the file at path, creating it if necessary. When mode is
// kSync, flushes and fsyncs before returning; throws std::system_error on
// any I/O failure (open, short write, flush, sync, or close).
void append_file_bytes(const std::string& path, const std::string& bytes, DurabilityMode mode);

}  // namespace simpledb
