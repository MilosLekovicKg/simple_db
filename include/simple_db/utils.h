#pragma once

#include <cstdint>
#include <iostream>
#include <string>

namespace simpledb {
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
}  // namespace simpledb