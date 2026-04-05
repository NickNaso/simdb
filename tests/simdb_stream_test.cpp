#include <gtest/gtest.h>
#include "simdb.hpp"
#include <string>
#include <vector>
#include <memory>

class SimdbStreamTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Create a unique DB for testing
        const testing::TestInfo* test_info = testing::UnitTest::GetInstance()->current_test_info();
        std::string dbName = std::string("simdb_stream_test_") + test_info->test_suite_name() + "_" + test_info->name();
        db = std::make_unique<simdb>(dbName.c_str(), 4096, 4096);
    }

    void TearDown() override {
        db.reset();
    }

    std::unique_ptr<simdb> db;
};

TEST_F(SimdbStreamTest, RoundTripBase) {
    std::string key = "stream_key1";
    std::string payload = "Hello, streaming world!";
    
    // Write
    auto ws = db->begin_write(key, payload.size());
    ASSERT_TRUE(ws.valid());
    EXPECT_TRUE(ws.write(payload.data(), payload.size()));
    EXPECT_TRUE(ws.commit());

    // Verify it's not empty and regular get works
    EXPECT_EQ(db->get(key), payload);

    // Read by stream
    std::string read_back;
    bool stream_success = db->read_stream(key, [&read_back](const void* chunk, uint32_t len) {
        read_back.append(static_cast<const char*>(chunk), len);
        return true;
    });

    EXPECT_TRUE(stream_success);
    EXPECT_EQ(read_back, payload);
}

TEST_F(SimdbStreamTest, CommitWithTrim) {
    std::string key = "trim_key";
    std::string payload = "short_value";
    
    // Allocate about 100 KB (much more than needed)
    uint32_t oversized_alloc = 100 * 1024;
    auto ws = db->begin_write(key, oversized_alloc);
    ASSERT_TRUE(ws.valid());
    
    EXPECT_TRUE(ws.write(payload.data(), payload.size()));
    // Commit only the written part, automatically trimming
    EXPECT_TRUE(ws.commit(payload.size()));

    // Verify size
    uint32_t vlen = 0;
    db->len(key, &vlen);
    EXPECT_EQ(vlen, payload.size());
    
    EXPECT_EQ(db->get(key), payload);
}

TEST_F(SimdbStreamTest, AbortDoesNotMakeEntryVisible) {
    std::string key = "abort_key";
    std::string payload = "secret_data";
    
    {
        auto ws = db->begin_write(key, payload.size());
        ASSERT_TRUE(ws.valid());
        ws.write(payload.data(), payload.size());
        // ws goes out of scope here -> abort() is called
    }

    // Key should not exist
    EXPECT_EQ(db->len(key), 0);
    EXPECT_EQ(db->get(key), "");
}

TEST_F(SimdbStreamTest, ExplicitAbort) {
    std::string key = "abort_key_exp";
    std::string payload = "secret_data2";
    
    auto ws = db->begin_write(key, payload.size());
    ASSERT_TRUE(ws.valid());
    ws.write(payload.data(), payload.size());
    ws.abort();
    
    // Commit should fail now
    EXPECT_FALSE(ws.commit());

    // Key should not exist
    EXPECT_EQ(db->len(key), 0);
}

TEST_F(SimdbStreamTest, BackpressurePoolEmpty) {
    // Create a very small DB: blockSize=64, blockCount=5
    // Each allocation will take at least 1 block for key + some for value
    const testing::TestInfo* test_info = testing::UnitTest::GetInstance()->current_test_info();
    std::string dbName = std::string("simdb_stream_small_") + test_info->test_suite_name() + "_" + test_info->name();
    simdb small_db(dbName.c_str(), 64, 5);
    
    // Fill up the DB with regular puts
    bool success = true;
    for(int i=0; i<4; ++i) {
        success = small_db.put("key_" + std::to_string(i), "val");
        if (!success) break;
    }
    
    // Now try to open a stream that requires more blocks than available
    auto ws = small_db.begin_write("big_stream", 1024);
    
    EXPECT_FALSE(ws.valid());
    EXPECT_FALSE(ws.write("data", 4));
    EXPECT_FALSE(ws.commit());
    EXPECT_EQ(small_db.error(), simdb_error::OUT_OF_SPACE);
}

TEST_F(SimdbStreamTest, MultipleChunks) {
    std::string key = "multi_chunk";
    std::vector<std::string> chunks = {
        "chunk1_",
        "chunk2_is_longer_",
        "c3",
        std::string(5000, 'x') // force across blocks with the fixture's 4096-byte block size
    };
    
    uint32_t total_size = 0;
    std::string expected;
    for (const auto& c : chunks) {
        total_size += c.size();
        expected += c;
    }

    auto ws = db->begin_write(key, total_size);
    ASSERT_TRUE(ws.valid());
    
    for (const auto& c : chunks) {
        EXPECT_TRUE(ws.write(c.data(), c.size()));
    }
    EXPECT_TRUE(ws.commit());

    std::string read_back;
    EXPECT_TRUE(db->read_stream(key, [&read_back](const void* c, uint32_t len) {
        read_back.append(static_cast<const char*>(c), len);
        return true;
    }));

    EXPECT_EQ(read_back, expected);
}

TEST_F(SimdbStreamTest, ReadStreamEarlyExit) {
    std::string key = "early_exit";
    std::string payload(5000, 'A'); // Requires multiple blocks
    
    db->put(key, payload);

    uint32_t total_read = 0;
    bool stream_success = db->read_stream(key, [&total_read](const void* chunk, uint32_t len) {
        total_read += len;
        // Exit after reading more than 1000 bytes
        if (total_read > 1000) {
            return false;
        }
        return true;
    });

    // stream_success should be false because we aborted
    EXPECT_FALSE(stream_success);
    // Should have stopped before reading all 5000 bytes
    EXPECT_LT(total_read, 5000);
}

TEST_F(SimdbStreamTest, LargeBinaryData) {
    std::string key = "large_binary";
    // 5MB of binary data
    std::vector<uint8_t> data(5 * 1024 * 1024);
    for (size_t i = 0; i < data.size(); ++i) {
        data[i] = static_cast<uint8_t>(i % 256 ^ (i >> 8));
    }

    auto ws = db->begin_write(key, data.size());
    ASSERT_TRUE(ws.valid());
    
    // Write in 1MB chunks
    const size_t chunk_size = 1024 * 1024;
    size_t written = 0;
    while (written < data.size()) {
        size_t to_write = std::min(chunk_size, data.size() - written);
        EXPECT_TRUE(ws.write(data.data() + written, to_write));
        written += to_write;
    }
    EXPECT_TRUE(ws.commit());

    std::vector<uint8_t> read_back;
    read_back.reserve(data.size());
    
    bool stream_success = db->read_stream(key, [&read_back](const void* chunk, uint32_t len) {
        const uint8_t* u8_chunk = static_cast<const uint8_t*>(chunk);
        read_back.insert(read_back.end(), u8_chunk, u8_chunk + len);
        return true; 
    });

    EXPECT_TRUE(stream_success);
    EXPECT_EQ(data.size(), read_back.size());
    EXPECT_EQ(data, read_back);
}
