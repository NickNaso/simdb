// mp_writer.cpp
// Helper process for multi-process SimDB integration tests.
//
// Usage:
//   mp_writer <db_name> <block_size> <block_count> <num_entries> <key_prefix> <value_prefix>
//
// Writes num_entries entries of the form:
//   key_prefix_0 -> value_prefix_0,  key_prefix_1 -> value_prefix_1, ...
// Exits 0 on success, 1 on any failure.

#include "simdb.hpp"

#include <cstdint>
#include <iostream>
#include <string>

int main(int argc, char* argv[]) {
    if (argc != 7) {
        std::cerr << "Usage: mp_writer <db_name> <block_size> <block_count>"
                     " <num_entries> <key_prefix> <value_prefix>\n";
        return 1;
    }

    const char* db_name = argv[1];
    uint32_t block_size = static_cast<uint32_t>(std::stoul(argv[2]));
    uint32_t block_count = static_cast<uint32_t>(std::stoul(argv[3]));
    int num_entries = std::stoi(argv[4]);
    std::string key_prefix = argv[5];
    std::string val_prefix = argv[6];

    simdb db(db_name, block_size, block_count);

    for (int i = 0; i < num_entries; ++i) {
        std::string key = key_prefix + "_" + std::to_string(i);
        std::string val = val_prefix + "_" + std::to_string(i);
        if (!db.put(key, val)) {
            std::cerr << "mp_writer: put() failed for key=" << key << "\n";
            return 1;
        }
    }

    return 0;
}
