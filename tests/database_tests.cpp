#include "simple_db/database.hpp"

#include <gtest/gtest.h>

#include <cstddef>
#include <optional>
#include <string>

#include "test_utils.h"

namespace {

using simpledb::test::TempDbFile;

TEST(DatabaseTest, MissingKey) {
  TempDbFile tmp("missing_key");
  simpledb::Database db(tmp.str());

  EXPECT_FALSE(db.get("unknown").has_value());
  EXPECT_FALSE(db.remove("unknown"));
  EXPECT_EQ(db.size(), size_t{0});
  EXPECT_TRUE(db.keys().empty());
}

TEST(DatabaseTest, Crud) {
  TempDbFile tmp("crud");
  simpledb::Database db(tmp.str());

  db.put("name", "Ada");
  EXPECT_EQ(db.get("name"), std::optional<std::string>("Ada"));
  EXPECT_TRUE(db.remove("name"));
  EXPECT_FALSE(db.get("name").has_value());
  EXPECT_FALSE(db.remove("name"));
}

TEST(DatabaseTest, OverwritePersistsAcrossReopen) {
  TempDbFile tmp("overwrite");
  {
    simpledb::Database db(tmp.str());
    db.put("name", "Ada");
    db.put("name", "Ada Lovelace");
    EXPECT_EQ(db.get("name"), std::optional<std::string>("Ada Lovelace"));
    EXPECT_EQ(db.size(), size_t{1});
  }

  {
    simpledb::Database db(tmp.str());
    EXPECT_EQ(db.get("name"), std::optional<std::string>("Ada Lovelace"));
    EXPECT_EQ(db.size(), size_t{1});
  }
}

TEST(DatabaseTest, OperationOrderPreservedOnReload) {
  TempDbFile tmp("operation_order");
  {
    simpledb::Database db(tmp.str());
    db.put("x", "1");
    db.put("x", "2");
    ASSERT_TRUE(db.remove("x"));
    db.put("x", "3");
  }

  {
    simpledb::Database db(tmp.str());
    EXPECT_EQ(db.get("x"), std::optional<std::string>("3"));
  }
}

TEST(DatabaseTest, KeysAreSortedAndSizeMatches) {
  TempDbFile tmp("keys");
  simpledb::Database db(tmp.str());
  db.put("name", "Ada");
  db.put("language", "C++");
  db.put("year", "1842");

  const auto keys = db.keys();
  ASSERT_EQ(keys.size(), size_t{3});
  EXPECT_EQ(keys[0], "language");
  EXPECT_EQ(keys[1], "name");
  EXPECT_EQ(keys[2], "year");
  EXPECT_EQ(db.size(), size_t{3});
}

TEST(DatabaseTest, EmptyAndSpecialValues) {
  TempDbFile tmp("empty_and_special_values");
  const std::string special_value("line\nwith\0null", 14);
  {
    simpledb::Database db(tmp.str());
    db.put("", "empty key");
    db.put("empty value", "");
    db.put("special", special_value);

    EXPECT_EQ(db.get(""), std::optional<std::string>("empty key"));
    EXPECT_EQ(db.get("empty value"), std::optional<std::string>(""));
    EXPECT_EQ(db.get("special"), std::optional<std::string>(special_value));
  }

  {
    simpledb::Database db(tmp.str());
    EXPECT_EQ(db.get(""), std::optional<std::string>("empty key"));
    EXPECT_EQ(db.get("empty value"), std::optional<std::string>(""));
    EXPECT_EQ(db.get("special"), std::optional<std::string>(special_value));
  }
}

}  // namespace
