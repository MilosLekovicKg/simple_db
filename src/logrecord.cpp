#include "simple_db/logrecord.h"
#include <cstdint>

namespace {
    inline void write_int32(std::ostream& os, int32_t value) {
        os.write(reinterpret_cast<const char*>(&value), sizeof(int32_t));
    }

    inline int32_t read_int32(std::istream& is) {
        int32_t value{};
        is.read(reinterpret_cast<char*>(&value), sizeof(int32_t));
        return value;
    }

    inline void write_string(std::ostream& os, const std::string& str) {
        int32_t size = static_cast<int32_t>(str.size());
        write_int32(os, size);
        os.write(str.data(), size);
    }

    inline std::string read_string(std::istream& is) {
        int32_t size = read_int32(is);
        std::string str(size, '\0');
        is.read(&str[0], size);
        return str;
    }
}

namespace simpledb {
LogRecord::LogRecord(LogRecordType type, std::string_view key, std::string_view value)
    : type_(type), key_(key), value_(value) {}

void LogRecord::serialize(std::ostream& os) const {
  const int32_t type_size = static_cast<int32_t>(sizeof(type_));
  write_int32(os, type_size);
  os.write(reinterpret_cast<const char*>(&type_), type_size);

  write_string(os, key_);
  write_string(os, value_);
}

void LogRecord::deserialize(std::istream& is) {
  int32_t type_size{};
  type_size = read_int32(is);
  LogRecordType type;
  is.read(reinterpret_cast<char*>(&type), type_size);

  std::string key = read_string(is);
  std::string value = read_string(is);

  this->type_ = type;
  this->key_ = std::move(key);
  this->value_ = std::move(value);
}

void LogRecord::flush_to_disk(const std::string& path) const {
    std::ofstream output(path, std::ios::binary | std::ios::app);
    if (!output.is_open()) {
        throw std::runtime_error("Failed to open log file for writing: " + path);
    }

    serialize(output);
}

LogRecord LogRecord::load_from_disk(std::istream& input) {
    if (!input.good()) {
        return LogRecord{};
    }

    LogRecord record;
    record.deserialize(input);
    return record;
}
}  // namespace simpledb