#include "simple_db/database.hpp"

#include <iostream>
#include <string>
#include <string_view>

namespace {

void print_usage(const char* program_name) {
  std::cout << "Usage: " << program_name << " <command> [args]\n";
  std::cout << "Commands:\n";
  std::cout << "  put <key> <value>\n";
  std::cout << "  get <key>\n";
  std::cout << "  delete <key>\n";
  std::cout << "  list\n";
}

}  // namespace

int main(int argc, char** argv) {
  simpledb::Database db("simple_db.db");

  if (argc < 2) {
    print_usage(argv[0]);
    return 0;
  }

  std::string_view command = argv[1];

  if (command == "put") {
    if (argc != 4) {
      std::cerr << "put requires exactly two arguments: <key> <value>\n";
      return 1;
    }
    db.put(argv[2], argv[3]);
    std::cout << "stored\n";
    return 0;
  }

  if (command == "get") {
    if (argc != 3) {
      std::cerr << "get requires exactly one argument: <key>\n";
      return 1;
    }

    auto value = db.get(argv[2]);
    if (value.has_value()) {
      std::cout << *value << '\n';
    } else {
      std::cout << "<missing>\n";
    }
    return 0;
  }

  if (command == "delete") {
    if (argc != 3) {
      std::cerr << "delete requires exactly one argument: <key>\n";
      return 1;
    }

    if (db.remove(argv[2])) {
      std::cout << "deleted\n";
    } else {
      std::cout << "<missing>\n";
    }
    return 0;
  }

  if (command == "list") {
    for (const auto& key : db.keys()) {
      std::cout << key << '\n';
    }
    return 0;
  }

  print_usage(argv[0]);
  return 1;
}
