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

TEST_F(SimdbTest, OutOfSpaceBehavior) {
    // Create a very small DB: blockSize=64, blockCount=5
    simdb small_db("simdb_test_small", 64, 5);
    
    bool success = true;
    for(int i=0; i<100; ++i) {
        success = small_db.put("key_" + std::to_string(i), "value_" + std::to_string(i));
        if (!success) break;
    }
    
    EXPECT_FALSE(success);
    EXPECT_EQ(small_db.error(), simdb_error::OUT_OF_SPACE);
}

TEST_F(SimdbTest, GetWithVersion) {
    db->put("versioned_key", "version_1");
    // Retrieve via getKeyStrs which returns VerStr structs
    std::vector<simdb::VerStr> keys = db->getKeyStrs();
    ASSERT_FALSE(keys.empty());

    std::string out_val;
    bool ok = db->get(keys[0], &out_val);
    
    EXPECT_TRUE(ok);
    EXPECT_EQ(out_val, "version_1");
    
    // Test the string returning overload
    EXPECT_EQ(db->get(keys[0]), "version_1");
}

TEST_F(SimdbTest, BinaryDataSerialization) {
    // Generate synthetic binary data (e.g., matching a raw image, protobuf, or dense numbers)
    std::vector<uint8_t> original_data(4096); 
    for (size_t i = 0; i < original_data.size(); ++i) {
        original_data[i] = static_cast<uint8_t>(i % 256);
    }
    
    // Store binary data natively through the STL vector template overload
    db->put("binary_payload", original_data);
    
    // Extract binary data identically via memory mapping
    std::vector<uint8_t> retrieved_data = db->get<uint8_t>("binary_payload");
    
    // Ensure accurate binary equivalence and retrieval lengths
    EXPECT_EQ(original_data.size(), retrieved_data.size());
    EXPECT_EQ(original_data, retrieved_data);
}
