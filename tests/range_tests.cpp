#include <gtest/gtest.h>

#include <optional>
#include <string>

#include "simple_db/database.hpp"
#include "test_utils.h"

namespace {

using simpledb::ScanResults;
using simpledb::test::TempDbFile;

void AddSampleRows(simpledb::Database& db) {
  db.put("alpha", "1");
  db.put("beta", "2");
  db.put("charlie", "3");
  db.put("delta", "4");
  db.put("user:1", "Ada");
  db.put("user:2", "Grace");
  db.put("user:x", "Edsger");
}

TEST(RangeTest, UsesInclusiveStartAndExclusiveEnd) {
  TempDbFile tmp("range_bounds");
  simpledb::Database db(tmp.str());
  AddSampleRows(db);

  EXPECT_EQ(db.scan(std::string_view("beta"), std::string_view("delta")),
            ScanResults({{"beta", "2"}, {"charlie", "3"}}));
  EXPECT_TRUE(db.scan(std::string_view("delta"), std::string_view("delta")).empty());
  EXPECT_TRUE(db.scan(std::string_view("z"), std::nullopt).empty());
}

TEST(RangeTest, SupportsUnboundedRangesAndLimits) {
  TempDbFile tmp("range_limits");
  simpledb::Database db(tmp.str());
  AddSampleRows(db);

  EXPECT_EQ(db.scan(std::nullopt, std::string_view("beta")),
            ScanResults({{"alpha", "1"}}));
  EXPECT_EQ(db.scan(std::string_view("charlie"), std::nullopt, 2),
            ScanResults({{"charlie", "3"}, {"delta", "4"}}));
  EXPECT_EQ(db.scan_after("beta", std::string_view("user:"), 2),
            ScanResults({{"charlie", "3"}, {"delta", "4"}}));
  EXPECT_EQ(db.scan(std::nullopt, std::nullopt, 1),
            ScanResults({{"alpha", "1"}}));
  EXPECT_EQ(db.scan(std::nullopt, std::nullopt, 100).size(), size_t{7});
}

TEST(RangeTest, PrefixScanHandlesEmptyAndHighBytePrefixes) {
  TempDbFile tmp("range_prefix");
  simpledb::Database db(tmp.str());
  db.put("user:a", "1");
  db.put("user:b", "2");
  db.put("users", "3");
  std::string high_byte_key("xff", 3);
  high_byte_key[0] = static_cast<char>(0xff);
  db.put(high_byte_key, "high");

  EXPECT_EQ(db.scan_prefix("user:"),
            ScanResults({{"user:a", "1"}, {"user:b", "2"}}));
  EXPECT_EQ(db.scan_prefix("user:", 1), ScanResults({{"user:a", "1"}}));
  EXPECT_EQ(db.scan_prefix(""), db.scan(std::nullopt, std::nullopt));

  std::string high_byte_prefix(1, static_cast<char>(0xff));
  EXPECT_EQ(db.scan_prefix(high_byte_prefix),
            ScanResults({{high_byte_key, "high"}}));
}

TEST(RangeTest, ResultsReflectUpdatesAndSurviveReload) {
  TempDbFile tmp("range_reload");
  {
    simpledb::Database db(tmp.str());
    AddSampleRows(db);
    db.put("beta", "updated");
    ASSERT_TRUE(db.remove("charlie"));
  }

  simpledb::Database db(tmp.str());
  EXPECT_EQ(db.scan(std::string_view("alpha"), std::string_view("delta")),
            ScanResults({{"alpha", "1"}, {"beta", "updated"}}));
}

}  // namespace
