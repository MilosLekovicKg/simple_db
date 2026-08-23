#include "simple_db/database.hpp"
#include "simple_db/logrecord.h"

#include <cassert>
#include <filesystem>
#include <fstream>
#include <string>

namespace {

std::filesystem::path test_path(const std::string& name) {
  return std::filesystem::temp_directory_path() / ("simple_db_" + name + ".db");
}

void remove_test_file(const std::filesystem::path& path) {
  std::filesystem::remove(path);
}

}  // namespace

int main() {
  {
    const auto path = test_path("missing_key");
    remove_test_file(path);
    simpledb::Database db(path.string());

    assert(!db.get("unknown").has_value());
    assert(!db.remove("unknown"));
    assert(db.size() == 0);
    assert(db.keys().empty());

    remove_test_file(path);
  }

  {
    const auto path = test_path("crud");
    remove_test_file(path);
    simpledb::Database db(path.string());

    db.put("name", "Ada");
    assert(db.get("name") == std::optional<std::string>("Ada"));
    assert(db.remove("name"));
    assert(!db.get("name").has_value());
    assert(!db.remove("name"));

    remove_test_file(path);
  }

  {
    const auto path = test_path("overwrite");
    remove_test_file(path);
    {
      simpledb::Database db(path.string());
      db.put("name", "Ada");
      db.put("name", "Ada Lovelace");
      assert(db.get("name") == std::optional<std::string>("Ada Lovelace"));
      assert(db.size() == 1);
    }

    {
      simpledb::Database db(path.string());
      assert(db.get("name") == std::optional<std::string>("Ada Lovelace"));
      assert(db.size() == 1);
    }

    remove_test_file(path);
  }

  {
    const auto path = test_path("operation_order");
    remove_test_file(path);
    {
      simpledb::Database db(path.string());
      db.put("x", "1");
      db.put("x", "2");
      assert(db.remove("x"));
      db.put("x", "3");
    }

    {
      simpledb::Database db(path.string());
      assert(db.get("x") == std::optional<std::string>("3"));
    }

    remove_test_file(path);
  }

  {
    const auto path = test_path("keys");
    remove_test_file(path);
    simpledb::Database db(path.string());
    db.put("name", "Ada");
    db.put("language", "C++");
    db.put("year", "1842");

    const auto keys = db.keys();
    assert(keys.size() == 3);
    assert(keys[0] == "language");
    assert(keys[1] == "name");
    assert(keys[2] == "year");

    const auto size = db.size();
    assert(size == 3);

    remove_test_file(path);
  }

  {
    const auto path = test_path("empty_and_special_values");
    remove_test_file(path);
    {
      simpledb::Database db(path.string());
      db.put("", "empty key");
      db.put("empty value", "");
      db.put("special", std::string("line\nwith\0null", 14));

      assert(db.get("") == std::optional<std::string>("empty key"));
      assert(db.get("empty value") == std::optional<std::string>(""));
      assert(db.get("special") ==
             std::optional<std::string>(std::string("line\nwith\0null", 14)));
    }

    {
      simpledb::Database db(path.string());
      assert(db.get("") == std::optional<std::string>("empty key"));
      assert(db.get("empty value") == std::optional<std::string>(""));
      assert(db.get("special") ==
             std::optional<std::string>(std::string("line\nwith\0null", 14)));
    }

    remove_test_file(path);
  }

  {
    const auto path = test_path("logrecord");
    remove_test_file(path);
    simpledb::LogRecord put_record(simpledb::LogRecordType::Put, "key", "value");
    simpledb::LogRecord remove_record(simpledb::LogRecordType::Remove, "key", "");
    put_record.flush_to_disk(path.string());
    remove_record.flush_to_disk(path.string());

    {
      std::ifstream input(path, std::ios::binary);
      const auto loaded_put = simpledb::LogRecord::load_from_disk(input);
      const auto loaded_remove = simpledb::LogRecord::load_from_disk(input);
      assert(loaded_put.type() == simpledb::LogRecordType::Put);
      assert(loaded_put.key() == "key");
      assert(loaded_put.value() == "value");
      assert(loaded_remove.type() == simpledb::LogRecordType::Remove);
      assert(loaded_remove.key() == "key");
      assert(loaded_remove.value().empty());
    }

    remove_test_file(path);
  }

  return 0;
}
