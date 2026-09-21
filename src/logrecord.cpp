#include "simple_db/logrecord.h"
#include "simple_db/utils.h"
#include <cstdint>
#include <sstream>
#include <stdexcept>

namespace simpledb {
namespace {
// Upper bound on a single record's payload. A larger length prefix means
// the file is corrupt; the cap avoids a huge allocation from a garbage
// length field.
constexpr int32_t kMaxRecordPayloadSize = 64 * 1024 * 1024;
}  // namespace

LogRecord::LogRecord(LogRecordType type, std::string_view key, std::string_view value, uint64_t lsn)
    : type_(type), key_(key), value_(value), lsn_(lsn) {}

// On-disk format: [int32 payload_size][payload bytes][uint32 crc32(payload)].
void LogRecord::serialize(std::ostream& os) const {
  std::ostringstream payload(std::ios::binary);
  const int32_t type_size = static_cast<int32_t>(sizeof(type_));
  write_int32(payload, type_size);
  payload.write(reinterpret_cast<const char*>(&type_), type_size);

  write_uint64(payload, lsn_);
  write_string(payload, key_);
  write_string(payload, value_);

  const std::string bytes = payload.str();
  write_int32(os, static_cast<int32_t>(bytes.size()));
  os.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
  write_uint32(os, crc32(bytes.data(), bytes.size()));
}

void LogRecord::deserialize(std::istream& is) {
  const int32_t payload_size = read_int32(is);
  if (!is.good() || payload_size < 0 || payload_size > kMaxRecordPayloadSize) {
    is.setstate(std::ios::failbit);
    return;
  }

  std::string payload(static_cast<std::size_t>(payload_size), '\0');
  is.read(payload.data(), payload_size);
  const uint32_t expected_crc = read_uint32(is);
  if (!is.good() || crc32(payload.data(), payload.size()) != expected_crc) {
    // Torn write (truncated bytes) or corrupted payload/checksum.
    is.setstate(std::ios::failbit);
    return;
  }

  std::istringstream payload_stream(payload, std::ios::binary);
  const int32_t type_size = read_int32(payload_stream);
  LogRecordType type{};
  payload_stream.read(reinterpret_cast<char*>(&type), type_size);
  const uint64_t lsn = read_uint64(payload_stream);
  std::string key = read_string(payload_stream);
  std::string value = read_string(payload_stream);
  if (payload_stream.fail()) {
    is.setstate(std::ios::failbit);
    return;
  }

  type_ = type;
  lsn_ = lsn;
  key_ = std::move(key);
  value_ = std::move(value);
}

void LogRecord::flush_to_disk(const std::string& path) const {
    std::ofstream output(path, std::ios::binary | std::ios::app);
    if (!output.is_open()) {
        throw std::runtime_error("Failed to open log file for writing: " + path);
    }

    serialize(output);
}

std::optional<LogRecord> LogRecord::load_from_disk(std::istream& input) {
    if (!input.good()) {
        return std::nullopt;
    }

    LogRecord record;
    record.deserialize(input);
    if (!input.good()) {
        return std::nullopt;
    }
    return record;
}
}  // namespace simpledb