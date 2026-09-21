#pragma once
#include <cstdint>
#include <fstream>
#include <optional>
#include <string>
#include <string_view>
#include "simple_db/serializable.h"

namespace simpledb {
enum class LogRecordType {
  Put,
  Remove
};

class LogRecord : public Serializable {
  public:
    LogRecord() = default;
    LogRecord(LogRecordType type, std::string_view key, std::string_view value, uint64_t lsn = 0);

    void flush_to_disk(const std::string& path) const;
    // Reads one framed record. Returns nullopt on a corrupt or torn record
    // (checksum mismatch or truncated bytes); callers detect clean EOF by
    // peeking before calling.
    static std::optional<LogRecord> load_from_disk(std::istream& input);

    const LogRecordType& type() const {
      return type_;
    }
    const std::string& key() const {
      return key_;
    }
    const std::string& value() const {
      return value_;
    }
    uint64_t lsn() const {
      return lsn_;
    }

  private:
    void serialize(std::ostream& os) const override;
    void deserialize(std::istream& is) override;
    LogRecordType type_;
    std::string key_;
    std::string value_;
    uint64_t lsn_ = 0;
};  // class LogRecord

}  // namespace simpledb