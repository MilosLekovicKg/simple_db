#pragma once

#include <ostream>
#include <istream>

class Serializable {
 public:
  virtual ~Serializable() = default;
  virtual void serialize(std::ostream& os) const = 0;
  virtual void deserialize(std::istream& is) = 0;
};