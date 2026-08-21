#include "simple_db/database.hpp"

#include <cassert>
#include <filesystem>

int main() {
  const auto path = std::filesystem::temp_directory_path() / "simple_db_test.db";
  std::filesystem::remove(path);

  {
    simpledb::Database db(path.string());
    db.put("name", "Ada");
  }

  {
    simpledb::Database db(path.string());
    const auto value = db.get("name");
    assert(value.has_value());
    assert(value.value() == "Ada");
  }

  {
    simpledb::Database db(path.string());
    const auto removed = db.remove("name");
    assert(removed);
    db.put("full_name", "Ada Lovelace");
  }

  {
    simpledb::Database db(path.string());
    const auto value = db.get("name");
    assert(!value.has_value());
  }

  {
    simpledb::Database db(path.string());
    db.put("name", "Ada");
    db.put("language", "C++");
    db.put("year", "1842");

    const auto keys = db.keys();
    assert(keys.size() == 4);
    assert(keys[0] == "full_name");
    assert(keys[1] == "language");
    assert(keys[2] == "name");
    assert(keys[3] == "year");

    const auto size = db.size();
    assert(size == 4);
  }
  
  std::filesystem::remove(path);

  return 0;
}
