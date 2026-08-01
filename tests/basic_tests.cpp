#include "simple_db/database.hpp"

#include <cassert>
#include <cstdio>
#include <iostream>

int main() {
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

  std::cout << "basic tests passed\n";
  return 0;
}
