#include "simple_db/database.hpp"

#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <string>
#include <vector>

namespace {

std::filesystem::path find_cli_path(const std::filesystem::path& test_binary_path) {
  const std::filesystem::path directory = test_binary_path.parent_path();
  const std::vector<std::filesystem::path> candidates = {
      directory / "simple_db_cli",
      directory / "simple_db_cli.exe",
      directory / "Debug/simple_db_cli.exe",
      directory / "Release/simple_db_cli.exe",
  };

  for (const auto& candidate : candidates) {
    if (std::filesystem::exists(candidate)) {
      return candidate;
    }
  }

  for (const auto& entry : std::filesystem::recursive_directory_iterator(directory)) {
    if (!entry.is_regular_file()) {
      continue;
    }

    const std::filesystem::path& path = entry.path();
    const std::string filename = path.filename().string();
    if (filename == "simple_db_cli" || filename == "simple_db_cli.exe") {
      return path;
    }
  }

  return directory / "simple_db_cli";
}

void run_cli_command(const std::filesystem::path& cli_path,
                     const std::filesystem::path& db_path,
                     const std::string& command,
                     std::string& output) {
  // const std::filesystem::path output_path = db_path.parent_path() / "cli_output.txt";
  // const std::string quoted_cli = std::quoted(cli_path.string()).str();
  // const std::string quoted_db = std::quoted(db_path.string()).str();
  // const std::string quoted_output = std::quoted(output_path.string()).str();
  // std::string shell_command = quoted_cli + " --db " + quoted_db + " " + command + " > " + quoted_output + " 2>&1";
  // const int exit_code = std::system(shell_command.c_str());
  // assert(exit_code == 0);

  // std::ifstream stream(output_path);
  // output.assign((std::istreambuf_iterator<char>(stream)), std::istreambuf_iterator<char>());
}

}  // namespace

int main(int argc, char** argv) {
  simpledb::Database db;

  db.put("alpha", "one");
  assert(db.get("alpha").value() == "one");
  assert(db.size() == 1);

  db.put("beta", "two");
  assert(db.get("beta").value() == "two");
  assert(db.size() == 2);

  assert(db.remove("alpha"));
  assert(!db.get("alpha").has_value());
  assert(db.size() == 1);

  db.clear();
  assert(db.size() == 0);

  const char* persistence_path = "test_persistence.db";
  std::remove(persistence_path);

  {
    simpledb::Database persisted_db(persistence_path);
    persisted_db.put("gamma", "three");
    persisted_db.put("delta", "four");
  }

  {
    simpledb::Database reopened_db(persistence_path);
    assert(reopened_db.get("gamma").value() == "three");
    assert(reopened_db.get("delta").value() == "four");
    assert(reopened_db.size() == 2);
  }

  const std::filesystem::path cli_path = find_cli_path(std::filesystem::absolute(argv[0]));
  const std::filesystem::path cli_db_path = std::filesystem::temp_directory_path() / "simple_db_cli_test.db";
  std::remove(cli_db_path.string().c_str());

  std::string output;
  run_cli_command(cli_path, cli_db_path, "put alpha one", output);
  assert(output == "stored\n");

  run_cli_command(cli_path, cli_db_path, "get alpha", output);
  assert(output == "one\n");

  run_cli_command(cli_path, cli_db_path, "list", output);
  assert(output == "alpha\n");

  std::cout << "basic tests passed\n";
  return 0;
}
