#pragma once

#include <ostream>
#include <istream>
#include <unordered_map>

using namespace std;

namespace simpledb {

inline void serialize_store(const unordered_map<string, string>& store, ostream& output) {
  const int32_t count = static_cast<int32_t>(store.size());
  output.write(reinterpret_cast<const char*>(&count), sizeof(count));

  for (const auto& [key, value] : store) {
    const int32_t key_size = static_cast<int32_t>(key.size());
    output.write(reinterpret_cast<const char*>(&key_size), sizeof(key_size));
    output.write(key.data(), key_size);

    const int32_t value_size = static_cast<int32_t>(value.size());
    output.write(reinterpret_cast<const char*>(&value_size), sizeof(value_size));
    output.write(value.data(), value_size);
  }
}

inline void deserialize_store(unordered_map<string, string>& store, istream& input) {
  int32_t count{};
  input.read(reinterpret_cast<char*>(&count), sizeof(count));

  for (int32_t i = 0; i < count; ++i) {
    int32_t key_size{};
    input.read(reinterpret_cast<char*>(&key_size), sizeof(key_size));
    string key(key_size, '\0');
    input.read(&key[0], key_size);

    int32_t value_size{};
    input.read(reinterpret_cast<char*>(&value_size), sizeof(value_size));
    string value(value_size, '\0');
    input.read(&value[0], value_size);

    store.emplace(std::move(key), std::move(value));
  }

}

}  // namespace simpledb