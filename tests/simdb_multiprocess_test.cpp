// simdb_multiprocess_test.cpp
//
// Integration tests that exercise SimDB with two or more OS processes sharing
// the same named shared-memory database.
//
// Architecture
// ============
// SimDB uses OS-level named shared memory (CreateFileMappingA on Windows;
// mmap+tmpfile on Linux/macOS). Every process that constructs a simdb object
// with the same name attaches to the same segment. The segment lives as long
// as at least one process holds it open (s_cnt > 0).
//
// The GTest fixture is always the *creator* of the segment (it opens first and
// becomes the owner). Child helper processes are spawned afterwards and join
// as non-owners. Because the fixture keeps the segment alive, child processes
// can exit and be restarted freely during a single test.
//
// Helper binaries are located via compile-time defines injected by CMake:
//   SIMDB_MP_WRITER_PATH         – absolute path to mp_writer executable
//   SIMDB_MP_READER_PATH         – absolute path to mp_reader executable
//   SIMDB_MP_CONC_WRITER_PATH    – absolute path to mp_concurrent_writer
//
// std::system() is used to launch child processes synchronously. For
// concurrent scenarios, std::thread runs multiple system() calls in parallel
// so the child processes overlap in real time.

#ifndef SIMDB_MP_WRITER_PATH
#error "SIMDB_MP_WRITER_PATH must be defined by the build system"
#endif
#ifndef SIMDB_MP_READER_PATH
#error "SIMDB_MP_READER_PATH must be defined by the build system"
#endif
#ifndef SIMDB_MP_CONC_WRITER_PATH
#error "SIMDB_MP_CONC_WRITER_PATH must be defined by the build system"
#endif

#include <gtest/gtest.h>
#include "simdb.hpp"

#include <atomic>
#include <cstdint>
#include <cstdlib>
#include <memory>
#include <string>
#include <thread>

// ---------------------------------------------------------------------------
// Utilities
// ---------------------------------------------------------------------------
namespace {

// Run a shell command synchronously. Returns true iff the child exits 0.
bool RunProcess(const std::string& cmd) {
    return std::system(cmd.c_str()) == 0;  // NOLINT(cert-env33-c)
}

// Wrap a path in double-quotes so embedded spaces are safe on both cmd.exe
// and POSIX shells.
std::string Q(const char* path) {
    return std::string("\"") + path + "\"";
}

constexpr uint32_t kBlockSize = 4096;
constexpr uint32_t kBlockCount = 256;
constexpr int kNumEntries = 10;
constexpr int kConcNum = 20;

}  // namespace

// ---------------------------------------------------------------------------
// Test fixture
// ---------------------------------------------------------------------------
class MultiProcessTest : public ::testing::Test {
protected:
    void SetUp() override {
        const auto* ti = testing::UnitTest::GetInstance()->current_test_info();

        // Build a unique, shell-safe shared-memory name for this test.
        db_name_ = std::string("mp_") + ti->test_suite_name() + "_" + ti->name() + "_" +
                   std::to_string(testing::UnitTest::GetInstance()->random_seed());

        // Replace any character that is not alphanumeric or underscore so that
        // the name is safe to pass as a shell argument without quoting.
        for (char& c : db_name_) {
            if (c != '_' && (c < 'A' || c > 'Z') && (c < 'a' || c > 'z') && (c < '0' || c > '9')) {
                c = '_';
            }
        }

        // Create the shared-memory segment first so this process becomes the
        // owner (s_cnt = 1). Children that open the same name become non-owners
        // and increment s_cnt. The segment is destroyed when this fixture tears
        // down and s_cnt reaches 0.
        db_ = std::make_unique<simdb>(db_name_.c_str(), kBlockSize, kBlockCount);
    }

    void TearDown() override { db_.reset(); }

    // Command builders

    std::string WriterCmd(const std::string& key_prefix, const std::string& val_prefix,
                          int num = kNumEntries) const {
        return Q(SIMDB_MP_WRITER_PATH) + " " + db_name_ + " " + std::to_string(kBlockSize) + " " +
               std::to_string(kBlockCount) + " " + std::to_string(num) + " " + key_prefix + " " + val_prefix;
    }

    std::string ReaderCmd(const std::string& key_prefix, const std::string& exp_prefix,
                          int num = kNumEntries) const {
        return Q(SIMDB_MP_READER_PATH) + " " + db_name_ + " " + std::to_string(kBlockSize) + " " +
               std::to_string(kBlockCount) + " " + std::to_string(num) + " " + key_prefix + " " + exp_prefix;
    }

    std::string ConcWriterCmd(const std::string& writer_id, int num = kConcNum) const {
        return Q(SIMDB_MP_CONC_WRITER_PATH) + " " + db_name_ + " " + std::to_string(kBlockSize) + " " +
               std::to_string(kBlockCount) + " " + std::to_string(num) + " " + writer_id;
    }

    std::unique_ptr<simdb> db_;
    std::string db_name_;
};

// ---------------------------------------------------------------------------
// Test 1 – WriterThenReader
//
// A child writer process writes N entries. After it exits, a separate child
// reader process verifies all N entries have the expected values.
// ---------------------------------------------------------------------------
TEST_F(MultiProcessTest, WriterThenReader) {
    ASSERT_TRUE(RunProcess(WriterCmd("key", "val"))) << "mp_writer child process exited with non-zero status";
    ASSERT_TRUE(RunProcess(ReaderCmd("key", "val")))
        << "mp_reader child process exited with non-zero status (value mismatch)";
}

// ---------------------------------------------------------------------------
// Test 2 – WriterVerifiedInProcess
//
// A child writer process writes N entries. After it exits, THIS (GTest)
// process reads them back through its own simdb handle.
// ---------------------------------------------------------------------------
TEST_F(MultiProcessTest, WriterVerifiedInProcess) {
    ASSERT_TRUE(RunProcess(WriterCmd("ikey", "ival"))) << "mp_writer child process failed";

    for (int i = 0; i < kNumEntries; ++i) {
        std::string key = "ikey_" + std::to_string(i);
        std::string expected = "ival_" + std::to_string(i);
        EXPECT_EQ(db_->get(key), expected) << "In-process get() mismatch for key=" << key;
    }
}

// ---------------------------------------------------------------------------
// Test 3 – ConcurrentWriters
//
// Two child writer processes (A and B) are launched in parallel via
// std::thread. Each uses a non-overlapping key namespace so there are no
// intentional collisions. After both exit, every entry must be readable.
// ---------------------------------------------------------------------------
TEST_F(MultiProcessTest, ConcurrentWriters) {
    std::atomic<bool> okA{false}, okB{false};

    std::thread tA([&]() { okA = RunProcess(ConcWriterCmd("A")); });
    std::thread tB([&]() { okB = RunProcess(ConcWriterCmd("B")); });

    tA.join();
    tB.join();

    ASSERT_TRUE(okA.load()) << "Concurrent writer-A process failed";
    ASSERT_TRUE(okB.load()) << "Concurrent writer-B process failed";

    for (int i = 0; i < kConcNum; ++i) {
        EXPECT_EQ(db_->get("wA_key_" + std::to_string(i)), "wA_val_" + std::to_string(i))
            << "Missing/corrupted entry from writer-A at index " << i;
        EXPECT_EQ(db_->get("wB_key_" + std::to_string(i)), "wB_val_" + std::to_string(i))
            << "Missing/corrupted entry from writer-B at index " << i;
    }
}

// ---------------------------------------------------------------------------
// Test 4 – ConcurrentReadWrite
//
// One child writer runs concurrently with the GTest process reading pre-
// populated keys. Verifies no crashes, deadlocks, or data corruption occur.
// ---------------------------------------------------------------------------
TEST_F(MultiProcessTest, ConcurrentReadWrite) {
    for (int i = 0; i < kConcNum; ++i) {
        ASSERT_TRUE(db_->put("rw_key_" + std::to_string(i), "rw_val_" + std::to_string(i)))
            << "Pre-populate put() failed at i=" << i;
    }

    std::atomic<bool> writer_ok{false};
    std::thread writer_thread([&]() { writer_ok = RunProcess(ConcWriterCmd("W", kConcNum)); });

    // Read pre-populated keys from THIS process while the child writer runs.
    // Values are not asserted here – the goal is no UB / crash.
    for (int round = 0; round < 5; ++round) {
        for (int i = 0; i < kConcNum; ++i) {
            (void)db_->get("rw_key_" + std::to_string(i));
        }
    }

    writer_thread.join();
    EXPECT_TRUE(writer_ok.load()) << "Concurrent writer-W process failed";

    for (int i = 0; i < kConcNum; ++i) {
        EXPECT_EQ(db_->get("wW_key_" + std::to_string(i)), "wW_val_" + std::to_string(i))
            << "Missing/corrupted entry from concurrent writer-W at index " << i;
    }
}

// ---------------------------------------------------------------------------
// Test 5 – WriterUpdateSeenInProcess
//
// A child process writes a set of keys, then a second child process
// overwrites them. The in-process handle must see only the latest values.
// ---------------------------------------------------------------------------
TEST_F(MultiProcessTest, WriterUpdateSeenInProcess) {
    constexpr int kUpd = 5;

    ASSERT_TRUE(RunProcess(WriterCmd("upd_key", "v1", kUpd))) << "Initial write (child) failed";

    for (int i = 0; i < kUpd; ++i) {
        EXPECT_EQ(db_->get("upd_key_" + std::to_string(i)), "v1_" + std::to_string(i))
            << "After initial write: unexpected value at index " << i;
    }

    ASSERT_TRUE(RunProcess(WriterCmd("upd_key", "v2", kUpd))) << "Update write (child) failed";

    for (int i = 0; i < kUpd; ++i) {
        EXPECT_EQ(db_->get("upd_key_" + std::to_string(i)), "v2_" + std::to_string(i))
            << "After update: in-process handle sees stale value at index " << i;
    }
}

// ---------------------------------------------------------------------------
// Test 6 – StreamWriteReadByChildProcess
//
// The GTest process writes a key using the streaming API. A child reader
// process then reads the same key and verifies the value.
// ---------------------------------------------------------------------------
TEST_F(MultiProcessTest, StreamWriteReadByChildProcess) {
    // mp_reader looks for "stream_key_0" and expects "stream_val_0".
    const std::string key = "stream_key_0";
    const std::string payload = "stream_val_0";

    auto ws = db_->begin_write(key, static_cast<uint32_t>(payload.size()));
    ASSERT_TRUE(ws.valid()) << "begin_write() failed (out of space?)";
    ASSERT_TRUE(ws.write(payload.data(), static_cast<uint32_t>(payload.size())));
    ASSERT_TRUE(ws.commit());

    EXPECT_EQ(db_->get(key), payload) << "In-process get() after stream write mismatch";

    ASSERT_TRUE(RunProcess(ReaderCmd("stream_key", "stream_val", 1)))
        << "mp_reader failed to read entry written via streaming API";
}

// ---------------------------------------------------------------------------
// Test 7 – LargePayloadStreamCrossProcess
//
// A multi-block (32 KB) payload is written in-process via the streaming API
// and verified with read_stream(). A small sentinel key is also written so a
// child reader can confirm the segment is readable from a separate process.
// ---------------------------------------------------------------------------
TEST_F(MultiProcessTest, LargePayloadStreamCrossProcess) {
    const std::string large_key = "large_stream_key";
    const std::string sentinel_key = "sentinel_key_0";
    const std::string sentinel_val = "sentinel_val_0";

    constexpr uint32_t kPayloadSize = 32u * 1024u;
    std::string payload(kPayloadSize, 'X');
    for (uint32_t i = 0; i < kPayloadSize; ++i) {
        payload[i] = static_cast<char>('A' + (i % 26));
    }

    {
        auto ws = db_->begin_write(large_key, kPayloadSize);
        ASSERT_TRUE(ws.valid()) << "begin_write() failed for large payload";

        constexpr uint32_t kChunk = 4096;
        uint32_t written = 0;
        while (written < kPayloadSize) {
            uint32_t to_write = std::min(kChunk, kPayloadSize - written);
            ASSERT_TRUE(ws.write(payload.data() + written, to_write));
            written += to_write;
        }
        ASSERT_TRUE(ws.commit());
    }

    std::string read_back;
    read_back.reserve(kPayloadSize);
    bool stream_ok = db_->read_stream(large_key, [&read_back](const void* chunk, uint32_t len) {
        read_back.append(static_cast<const char*>(chunk), len);
        return true;
    });
    ASSERT_TRUE(stream_ok);
    ASSERT_EQ(read_back, payload);

    ASSERT_TRUE(db_->put(sentinel_key, sentinel_val));

    ASSERT_TRUE(RunProcess(ReaderCmd("sentinel_key", "sentinel_val", 1)))
        << "Child reader failed after large stream write";
}
