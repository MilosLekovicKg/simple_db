#include "simple_db/database.hpp"

#include <iostream>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

namespace {

void print_usage(const char* program_name) {
  std::cout << "Usage: " << program_name << " [--db <path>] [command [args]]\n";
  std::cout << "Commands:\n";
  std::cout << "  put <key> <value>\n";
  std::cout << "  get <key>\n";
  std::cout << "  delete <key>\n";
  std::cout << "  list\n";
  std::cout << "  help\n";
  std::cout << "If no command is provided, the program starts an interactive shell.\n";
}

int run_command(simpledb::Database& db, const std::vector<std::string>& args) {
  if (args.empty()) {
    return 0;
  }

  const std::string& command = args[0];

  if (command == "put") {
    if (args.size() != 3) {
      std::cerr << "put requires exactly two arguments: <key> <value>\n";
      return 1;
    }
    db.put(args[1], args[2]);
    std::cout << "stored\n";
    return 0;
  }

  if (command == "get") {
    if (args.size() != 2) {
      std::cerr << "get requires exactly one argument: <key>\n";
      return 1;
    }

    auto value = db.get(args[1]);
    if (value.has_value()) {
      std::cout << *value << '\n';
    } else {
      std::cout << "<missing>\n";
    }
    return 0;
  }

  if (command == "delete") {
    if (args.size() != 2) {
      std::cerr << "delete requires exactly one argument: <key>\n";
      return 1;
    }

    if (db.remove(args[1])) {
      std::cout << "deleted\n";
    } else {
      std::cout << "<missing>\n";
    }
    return 0;
  }

  if (command == "list") {
    if (args.size() != 1) {
      std::cerr << "list does not take any arguments\n";
      return 1;
    }

    for (const auto& key : db.keys()) {
      std::cout << key << '\n';
    }
    return 0;
  }

  if (command == "help") {
    print_usage("simple_db_cli");
    return 0;
  }

  std::cerr << "unknown command: " << command << '\n';
  print_usage("simple_db_cli");
  return 1;
}

int run_interactive(simpledb::Database& db) {
  std::cout << "simple_db> ";
  std::string line;
  while (std::getline(std::cin, line)) {
    if (line.empty()) {
      std::cout << "simple_db> ";
      continue;
    }

    if (line == "quit" || line == "exit") {
      break;
    }

    std::istringstream stream(line);
    std::vector<std::string> args;
    std::string token;
    while (stream >> token) {
      args.push_back(token);
    }

    if (run_command(db, args) != 0) {
      return 1;
    }

    std::cout << "simple_db> ";
  }

  return 0;
}

}  // namespace

int main(int argc, char** argv) {
  std::string db_path = "simple_db.db";
  std::vector<std::string> args;
  bool interactive = true;

  for (int i = 1; i < argc; ++i) {
    const std::string_view current = argv[i];
    if (current == "--db") {
      if (i + 1 >= argc) {
        std::cerr << "--db requires a path\n";
        return 1;
      }
      db_path = argv[++i];
      continue;
    }
    if (current == "--help" || current == "-h") {
      print_usage(argv[0]);
      return 0;
    }
    if (current == "--interactive" || current == "-i") {
      interactive = true;
      continue;
    }

    interactive = false;
    args.emplace_back(current);
  }

  simpledb::Database db(db_path);

  if (interactive) {
    return run_interactive(db);
  }

  return run_command(db, args);
}
