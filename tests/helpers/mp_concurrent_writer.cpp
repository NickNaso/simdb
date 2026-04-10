// mp_concurrent_writer.cpp
// Helper process for multi-process SimDB concurrent-write integration tests.
//
// Usage:
//   mp_concurrent_writer <db_name> <block_size> <block_count> <num_entries> <writer_id>
//
// Writes num_entries entries tagged with writer_id:
//   w<writer_id>_key_0 -> w<writer_id>_val_0, ...
//
// Keys are non-overlapping across different writer_ids so multiple instances
// can run concurrently without intentional key conflicts.
// Exits 0 on success, 1 on any failure.

#include "simdb.hpp"

#include <cstdint>
#include <iostream>
#include <string>

int main(int argc, char* argv[]) {
    if (argc != 6) {
        std::cerr << "Usage: mp_concurrent_writer <db_name> <block_size>"
                     " <block_count> <num_entries> <writer_id>\n";
        return 1;
    }

    const char* db_name = argv[1];
    uint32_t block_size = static_cast<uint32_t>(std::stoul(argv[2]));
    uint32_t block_count = static_cast<uint32_t>(std::stoul(argv[3]));
    int num_entries = std::stoi(argv[4]);
    std::string writer_id = argv[5];

    simdb db(db_name, block_size, block_count);

    for (int i = 0; i < num_entries; ++i) {
        std::string key = "w" + writer_id + "_key_" + std::to_string(i);
        std::string val = "w" + writer_id + "_val_" + std::to_string(i);
        if (!db.put(key, val)) {
            std::cerr << "mp_concurrent_writer[" << writer_id << "]: put() failed for key=" << key << "\n";
            return 1;
        }
    }

    return 0;
}
