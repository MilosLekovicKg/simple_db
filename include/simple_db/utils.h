#pragma once

#include <array>
#include <cstddef>
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

    inline void write_uint32(std::ostream& os, uint32_t value) {
        os.write(reinterpret_cast<const char*>(&value), sizeof(uint32_t));
    }

    inline uint32_t read_uint32(std::istream& is) {
        uint32_t value{};
        is.read(reinterpret_cast<char*>(&value), sizeof(uint32_t));
        return value;
    }

    // CRC-32 (IEEE 802.3 polynomial) of a byte buffer, table-driven.
    inline uint32_t crc32(const char* data, std::size_t size) {
        static const std::array<uint32_t, 256> table = [] {
            std::array<uint32_t, 256> t{};
            for (uint32_t i = 0; i < 256; ++i) {
                uint32_t c = i;
                for (int bit = 0; bit < 8; ++bit) {
                    c = (c & 1u) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
                }
                t[i] = c;
            }
            return t;
        }();

        uint32_t crc = 0xFFFFFFFFu;
        for (std::size_t i = 0; i < size; ++i) {
            crc = table[(crc ^ static_cast<unsigned char>(data[i])) & 0xFFu] ^ (crc >> 8);
        }
        return crc ^ 0xFFFFFFFFu;
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

    inline void write_uint64(std::ostream& os, uint64_t value) {
        os.write(reinterpret_cast<const char*>(&value), sizeof(uint64_t));
    }

    inline uint64_t read_uint64(std::istream& is) {
        uint64_t value{};
        is.read(reinterpret_cast<char*>(&value), sizeof(uint64_t));
        return value;
    }
}  // namespace simpledb