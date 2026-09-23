#include "simple_db/logrecord.h"

#include <gtest/gtest.h>

#include <fstream>

#include "test_utils.h"

namespace {

using simpledb::test::TempDbFile;

TEST(LogRecordTest, FlushAndLoadRoundTrip) {
  TempDbFile tmp("logrecord");

  simpledb::LogRecord put_record(simpledb::LogRecordType::Put, "key", "value");
  simpledb::LogRecord remove_record(simpledb::LogRecordType::Remove, "key", "");
  put_record.flush_to_disk(tmp.str());
  remove_record.flush_to_disk(tmp.str());

  std::ifstream input(tmp.path(), std::ios::binary);
  const auto loaded_put = simpledb::LogRecord::load_from_disk(input);
  const auto loaded_remove = simpledb::LogRecord::load_from_disk(input);

  ASSERT_TRUE(loaded_put.has_value());
  EXPECT_EQ(loaded_put->type(), simpledb::LogRecordType::Put);
  EXPECT_EQ(loaded_put->key(), "key");
  EXPECT_EQ(loaded_put->value(), "value");

  ASSERT_TRUE(loaded_remove.has_value());
  EXPECT_EQ(loaded_remove->type(), simpledb::LogRecordType::Remove);
  EXPECT_EQ(loaded_remove->key(), "key");
  EXPECT_TRUE(loaded_remove->value().empty());
}

}  // namespace
