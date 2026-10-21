#include "simple_db/durable_file.h"

#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <filesystem>
#include <mutex>
#include <system_error>
#include <utility>

#ifdef _WIN32
#include <io.h>
#include <windows.h>
#else
#include <fcntl.h>
#include <unistd.h>
#endif

namespace simpledb {
namespace {

std::mutex fault_mutex;
DurableFileFaultInjector fault_injector;

[[noreturn]] void throw_errno(const std::string& path, const char* op) {
  throw std::system_error(errno, std::generic_category(), std::string(op) + " failed for " + path);
}

std::optional<std::size_t> inject_fault(DurableFileOperation operation,
                                        const std::string& path,
                                        std::size_t requested) {
  DurableFileFaultInjector injector;
  {
    std::lock_guard<std::mutex> lock(fault_mutex);
    injector = fault_injector;
  }
  if (!injector) {
    return std::nullopt;
  }
  return injector(operation, path, requested);
}

void throw_injected_sync_failure(const std::string& path) {
  throw std::system_error(std::make_error_code(std::errc::io_error),
                          "injected sync failure for " + path);
}

}  // namespace

void set_durable_file_fault_injector(DurableFileFaultInjector injector) {
  std::lock_guard<std::mutex> lock(fault_mutex);
  fault_injector = std::move(injector);
}

void clear_durable_file_fault_injector() {
  std::lock_guard<std::mutex> lock(fault_mutex);
  fault_injector = nullptr;
}

void write_file_bytes(const std::string& path, const std::string& bytes, DurabilityMode mode) {
#ifdef _WIN32
  std::FILE* file = nullptr;
  if (fopen_s(&file, path.c_str(), "wb") != 0 || file == nullptr) {
    throw_errno(path, "open");
  }
#else
  std::FILE* file = std::fopen(path.c_str(), "wb");
  if (file == nullptr) {
    throw_errno(path, "open");
  }
#endif

  const std::size_t requested_write_size =
      inject_fault(DurableFileOperation::kWrite, path, bytes.size())
          .value_or(bytes.size());
  const std::size_t write_size =
      requested_write_size < bytes.size() ? requested_write_size : bytes.size();
  if (std::fwrite(bytes.data(), 1, write_size, file) != write_size ||
      write_size != bytes.size()) {
    std::fclose(file);
    throw std::system_error(std::make_error_code(std::errc::io_error),
                            "short write to " + path);
  }
  if (std::fflush(file) != 0) {
    std::fclose(file);
    throw_errno(path, "flush");
  }
  if (mode == DurabilityMode::kSync) {
    if (inject_fault(DurableFileOperation::kSync, path, 0).has_value()) {
      std::fclose(file);
      throw_injected_sync_failure(path);
    }
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

  const std::size_t requested_write_size =
      inject_fault(DurableFileOperation::kWrite, path, bytes.size())
          .value_or(bytes.size());
  const std::size_t write_size =
      requested_write_size < bytes.size() ? requested_write_size : bytes.size();
  const std::size_t written = std::fwrite(bytes.data(), 1, write_size, file);
  if (written != bytes.size() || write_size != bytes.size()) {
    std::fclose(file);
    throw std::system_error(std::make_error_code(std::errc::io_error), "short write to " + path);
  }

  if (std::fflush(file) != 0) {
    std::fclose(file);
    throw_errno(path, "flush");
  }

  if (mode == DurabilityMode::kSync) {
    if (inject_fault(DurableFileOperation::kSync, path, 0).has_value()) {
      std::fclose(file);
      throw_injected_sync_failure(path);
    }
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

void replace_file(const std::string& temporary_path, const std::string& target_path,
                  DurabilityMode mode) {
  if (mode == DurabilityMode::kSync) {
    if (inject_fault(DurableFileOperation::kSync, temporary_path, 0).has_value()) {
      throw_injected_sync_failure(temporary_path);
    }
#ifdef _WIN32
    std::FILE* file = nullptr;
    if (fopen_s(&file, temporary_path.c_str(), "ab") != 0 || file == nullptr) {
      throw_errno(temporary_path, "open");
    }
    const int fd = _fileno(file);
    if (fd == -1 || _commit(fd) != 0) {
      std::fclose(file);
      throw_errno(temporary_path, "commit");
    }
    if (std::fclose(file) != 0) {
      throw_errno(temporary_path, "close");
    }
#else
    std::FILE* file = std::fopen(temporary_path.c_str(), "ab");
    if (file == nullptr) {
      throw_errno(temporary_path, "open");
    }
    const int fd = fileno(file);
    if (fd == -1 || fsync(fd) != 0) {
      std::fclose(file);
      throw_errno(temporary_path, "fsync");
    }
    if (std::fclose(file) != 0) {
      throw_errno(temporary_path, "close");
    }
#endif
  }

#ifdef _WIN32
  if (!MoveFileExW(std::filesystem::path(temporary_path).wstring().c_str(),
                   std::filesystem::path(target_path).wstring().c_str(),
                   MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
    throw_errno(target_path, "replace");
  }
#else
  std::error_code ec;
  std::filesystem::rename(temporary_path, target_path, ec);
  if (ec) {
    throw std::system_error(ec, "rename failed from " + temporary_path + " to " + target_path);
  }
#endif

  if (mode == DurabilityMode::kSync) {
    const auto parent = std::filesystem::path(target_path).parent_path();
    const auto directory = parent.empty() ? std::filesystem::path(".") : parent;
#ifdef _WIN32
    // Windows has no portable directory-fsync equivalent. The
    // MOVEFILE_WRITE_THROUGH flag above requests that the rename and its
    // metadata be flushed before returning.
    (void)directory;
#else
    const int fd = ::open(directory.string().c_str(), O_RDONLY | O_DIRECTORY);
    if (fd == -1 || fsync(fd) != 0) {
      if (fd != -1) {
        close(fd);
      }
      throw_errno(directory.string(), "fsync directory");
    }
    close(fd);
#endif
  }
}

}  // namespace simpledb
