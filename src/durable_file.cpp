#include "simple_db/durable_file.h"

#include <cerrno>
#include <cstdio>
#include <system_error>

#ifdef _WIN32
#include <io.h>
#else
#include <unistd.h>
#endif

namespace simpledb {
namespace {

[[noreturn]] void throw_errno(const std::string& path, const char* op) {
  throw std::system_error(errno, std::generic_category(), std::string(op) + " failed for " + path);
}

}  // namespace

void append_file_bytes(const std::string& path, const std::string& bytes, DurabilityMode mode) {
#ifdef _WIN32
  std::FILE* file = nullptr;
  if (fopen_s(&file, path.c_str(), "ab") != 0 || file == nullptr) {
    throw_errno(path, "open");
  }
#else
  std::FILE* file = std::fopen(path.c_str(), "ab");
  if (file == nullptr) {
    throw_errno(path, "open");
  }
#endif

  const std::size_t written = std::fwrite(bytes.data(), 1, bytes.size(), file);
  if (written != bytes.size()) {
    std::fclose(file);
    throw std::system_error(std::make_error_code(std::errc::io_error), "short write to " + path);
  }

  if (std::fflush(file) != 0) {
    std::fclose(file);
    throw_errno(path, "flush");
  }

  if (mode == DurabilityMode::kSync) {
#ifdef _WIN32
    const int fd = _fileno(file);
    if (fd == -1 || _commit(fd) != 0) {
      std::fclose(file);
      throw_errno(path, "commit");
    }
#else
    const int fd = fileno(file);
    if (fd == -1 || fsync(fd) != 0) {
      std::fclose(file);
      throw_errno(path, "fsync");
    }
#endif
  }

  if (std::fclose(file) != 0) {
    throw_errno(path, "close");
  }
}

}  // namespace simpledb
