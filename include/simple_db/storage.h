#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>
#include "simple_db/serializable.h"

namespace simpledb {
class Storage : public Serializable {
 public:
  virtual ~Storage() = default;

  virtual void put(std::string_view key, std::string_view value);
  virtual std::optional<std::string> get(std::string_view key);
  virtual bool remove(std::string_view key);
  std::vector<std::string> keys() const;
  std::size_t size() const;

  void snapshot(std::ostream& os) const {
    serialize(os);
  }

  void load_snapshot(std::istream& is) {
    deserialize(is);
  }

 private:
  std::unordered_map<std::string, std::string> store_;

  void serialize(std::ostream& os) const override;
  void deserialize(std::istream& is) override;
};
}; // namespace simpledb