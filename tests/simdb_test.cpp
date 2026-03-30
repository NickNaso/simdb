#include <gtest/gtest.h>
#include "simdb.hpp"
#include <string>
#include <vector>
#include <memory>

class SimdbTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Create a unique DB for testing
        std::string dbName = "simdb_test_" + std::to_string(testing::UnitTest::GetInstance()->random_seed());
        db = std::make_unique<simdb>(dbName.c_str(), 1024, 4096);
    }

    void TearDown() override {
        db.reset();
    }

    std::unique_ptr<simdb> db;
};

TEST_F(SimdbTest, InitializationIsSuccessful) {
    ASSERT_NE(db, nullptr);
}

TEST_F(SimdbTest, PutAndGetString) {
    db->put("test_key", "test_value");
    std::string val = db->get("test_key");
    EXPECT_EQ(val, "test_value");
}

TEST_F(SimdbTest, DeleteRemovesKey) {
    db->put("key_to_delete", "val");
    EXPECT_EQ(db->get("key_to_delete"), "val");
    
    db->del("key_to_delete");
    EXPECT_EQ(db->get("key_to_delete"), "");
}

TEST_F(SimdbTest, ListDatabases) {
    auto dbs = simdb_listDBs();
    EXPECT_FALSE(dbs.empty());
}
