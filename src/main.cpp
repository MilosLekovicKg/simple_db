#include "simple_db/database.hpp"
#include "simple_db/durability.h"

#include <charconv>
#include <iostream>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

namespace {

void print_usage(const char* program_name) {
  std::cout << "Usage: " << program_name << " [--db <path>] [--durability <async|sync>] [command [args]]\n";
  std::cout << "  --durability async|sync  durability of WAL appends (default: sync)\n";
  std::cout << "Commands:\n";
  std::cout << "  put <key> <value>\n";
  std::cout << "  get <key>\n";
  std::cout << "  delete <key>\n";
  std::cout << "  list\n";
  std::cout << "  range <start|-> <end|-> [limit]\n";
  std::cout << "  prefix <prefix>\n";
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

  if (command == "range") {
    if (args.size() != 3 && args.size() != 4) {
      std::cerr << "range requires <start|-> <end|-> [limit]\n";
      return 1;
    }
    std::optional<std::string_view> start;
    std::optional<std::string_view> end;
    if (args[1] != "-") {
      start = args[1];
    }
    if (args[2] != "-") {
      end = args[2];
    }
    std::size_t limit = 0;
    if (args.size() == 4) {
      const auto* first = args[3].data();
      const auto* last = first + args[3].size();
      const auto parsed = std::from_chars(first, last, limit);
      if (parsed.ec != std::errc{} || parsed.ptr != last) {
        std::cerr << "range limit must be a non-negative integer\n";
        return 1;
      }
    }
    for (const auto& [key, value] : db.scan(start, end, limit)) {
      std::cout << key << '=' << value << '\n';
    }
    return 0;
  }

  if (command == "prefix") {
    if (args.size() != 2) {
      std::cerr << "prefix requires exactly one argument: <prefix>\n";
      return 1;
    }
    for (const auto& [key, value] : db.scan_prefix(args[1])) {
      std::cout << key << '=' << value << '\n';
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
  simpledb::DurabilityMode durability = simpledb::DurabilityMode::kSync;

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
    if (current == "--durability") {
      if (i + 1 >= argc) {
        std::cerr << "--durability requires a value: async or sync\n";
        return 1;
      }
      const auto parsed = simpledb::parse_durability_mode(argv[++i]);
      if (!parsed.has_value()) {
        std::cerr << "invalid --durability value; expected async or sync\n";
        return 1;
      }
      durability = *parsed;
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

  simpledb::Database db(db_path, durability);

  if (interactive) {
    return run_interactive(db);
  }

  return run_command(db, args);
}
