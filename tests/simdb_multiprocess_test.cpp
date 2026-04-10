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
//
// Cross-platform constraints
// ==========================
// On Linux/macOS the shared segment is backed by a file in P_tmpdir. After a
// child process exits, a subsequent child process joining the same segment
// relies on the file still being present and on the owner-detection heuristic
// (open(O_RDWR) succeeds -> non-owner). Tests are therefore designed so that:
//   • only ONE child process writes in any given scenario, OR
//   • all writes go through the GTest (parent) process, which holds the segment
//     open for the full test duration and is guaranteed to be the true owner.
//
// The reliably cross-platform patterns are:
//   (a) child writes  → GTest reads          (see WriterVerifiedInProcess)
//   (b) GTest writes  → child reads          (see ParentWritesChildReads)
//   (c) GTest + ONE concurrent child write   (see ConcurrentReadWrite,
//                                              ConcurrentParentAndChildWrite)
//   (d) GTest writes  → GTest updates        (see ParentUpdateChildVerifies)
//       → child reads updated value

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
// Test 1 – WriterVerifiedInProcess
//
// A child writer process writes N entries. After it exits, THIS (GTest)
// process reads them back through its own simdb handle.
//
// Pattern (a): child writes → GTest reads.
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
// Test 2 – ParentWritesChildReads
//
// The GTest process writes N entries using its own simdb handle. A child
// reader process then reads and verifies every entry.
//
// Pattern (b): GTest writes → child reads.
// ---------------------------------------------------------------------------
TEST_F(MultiProcessTest, ParentWritesChildReads) {
    for (int i = 0; i < kNumEntries; ++i) {
        std::string key = "pkey_" + std::to_string(i);
        std::string val = "pval_" + std::to_string(i);
        ASSERT_TRUE(db_->put(key, val)) << "GTest put() failed at i=" << i;
    }

    ASSERT_TRUE(RunProcess(ReaderCmd("pkey", "pval")))
        << "Child reader failed to read parent-written entries";
}

// ---------------------------------------------------------------------------
// Test 3 – ConcurrentReadWrite
//
// One child writer runs concurrently with the GTest process reading pre-
// populated keys. Verifies no crashes, deadlocks, or data corruption occur.
//
// Pattern (c): GTest + ONE concurrent child write.
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
// Test 4 – ConcurrentParentAndChildWrite
//
// The GTest process writes a set of keys in a background thread while one
// child writer process concurrently writes a disjoint set of keys. After
// both complete, the GTest process verifies both sets.
//
// Pattern (c): GTest + ONE concurrent child write, GTest reads both after.
// ---------------------------------------------------------------------------
TEST_F(MultiProcessTest, ConcurrentParentAndChildWrite) {
    std::atomic<bool> child_ok{false};

    // Start the child writer in a thread so it overlaps with GTest writing.
    std::thread child_thread([&]() { child_ok = RunProcess(ConcWriterCmd("E", kConcNum)); });

    // GTest concurrently writes its own disjoint set of keys.
    for (int i = 0; i < kConcNum; ++i) {
        EXPECT_TRUE(db_->put("wG_key_" + std::to_string(i), "wG_val_" + std::to_string(i)))
            << "GTest concurrent put() failed at i=" << i;
    }

    child_thread.join();
    ASSERT_TRUE(child_ok.load()) << "Concurrent child writer-E failed";

    // Verify GTest's own writes.
    for (int i = 0; i < kConcNum; ++i) {
        EXPECT_EQ(db_->get("wG_key_" + std::to_string(i)), "wG_val_" + std::to_string(i))
            << "GTest's own concurrent entry missing at index " << i;
    }

    // Verify child writer's writes (pattern (a): child writes → GTest reads).
    for (int i = 0; i < kConcNum; ++i) {
        EXPECT_EQ(db_->get("wE_key_" + std::to_string(i)), "wE_val_" + std::to_string(i))
            << "Child writer-E entry missing at index " << i;
    }
}

// ---------------------------------------------------------------------------
// Test 5 – ParentUpdateChildVerifies
//
// The GTest process writes a set of keys (v1), then a child process reads
// and verifies the initial values (v1). The GTest process then overwrites
// the same keys with new values (v2), and a child process verifies the
// updated values (v2).
//
// Pattern (d): GTest writes → GTest updates → child reads updated value.
// ---------------------------------------------------------------------------
TEST_F(MultiProcessTest, ParentUpdateChildVerifies) {
    constexpr int kUpd = 5;

    // GTest writes initial values (v1).
    for (int i = 0; i < kUpd; ++i) {
        ASSERT_TRUE(db_->put("upd_key_" + std::to_string(i), "v1_" + std::to_string(i)))
            << "Initial put() failed at i=" << i;
    }

    // Child verifies initial values.
    ASSERT_TRUE(RunProcess(ReaderCmd("upd_key", "v1", kUpd)))
        << "Child reader failed to read initial (v1) values";

    // GTest overwrites with v2.
    for (int i = 0; i < kUpd; ++i) {
        ASSERT_TRUE(db_->put("upd_key_" + std::to_string(i), "v2_" + std::to_string(i)))
            << "Update put() failed at i=" << i;
    }

    // Child verifies updated values.
    ASSERT_TRUE(RunProcess(ReaderCmd("upd_key", "v2", kUpd)))
        << "Child reader failed to read updated (v2) values";
}

// ---------------------------------------------------------------------------
// Test 6 – StreamWriteReadByChildProcess
//
// The GTest process writes a key using the streaming API. A child reader
// process then reads the same key and verifies the value.
//
// Pattern (b): GTest writes (via streaming) → child reads.
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
//
// Pattern (b): GTest writes (large stream) → child reads sentinel.
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
