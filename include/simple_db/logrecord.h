#pragma once
#include <fstream>
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
    LogRecord(LogRecordType type, std::string_view key, std::string_view value);

    void flush_to_disk(const std::string& path) const;
    static LogRecord load_from_disk(std::istream& input);

    const LogRecordType& type() const {
      return type_;
    }
    const std::string& key() const {
      return key_;
    }
    const std::string& value() const {
      return value_;
    }

  private:
    void serialize(std::ostream& os) const override;
    void deserialize(std::istream& is) override;
    LogRecordType type_;
    std::string key_;
    std::string value_;
};  // class LogRecord

}  // namespace simpledb