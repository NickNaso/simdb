// mp_reader.cpp
// Helper process for multi-process SimDB integration tests.
//
// Usage:
//   mp_reader <db_name> <block_size> <block_count> <num_entries> <key_prefix> <expected_value_prefix>
//
// Reads num_entries entries and verifies each one:
//   key_prefix_0 should equal expected_value_prefix_0, ...
// Exits 0 if all values match, 1 on any mismatch or missing key.

#include "simdb.hpp"

#include <cstdint>
#include <iostream>
#include <string>

int main(int argc, char* argv[]) {
    if (argc != 7) {
        std::cerr << "Usage: mp_reader <db_name> <block_size> <block_count>"
                     " <num_entries> <key_prefix> <expected_value_prefix>\n";
        return 1;
    }

    const char* db_name = argv[1];
    uint32_t block_size = static_cast<uint32_t>(std::stoul(argv[2]));
    uint32_t block_count = static_cast<uint32_t>(std::stoul(argv[3]));
    int num_entries = std::stoi(argv[4]);
    std::string key_prefix = argv[5];
    std::string exp_prefix = argv[6];

    simdb db(db_name, block_size, block_count);

    int failures = 0;

    for (int i = 0; i < num_entries; ++i) {
        std::string key = key_prefix + "_" + std::to_string(i);
        std::string expected = exp_prefix + "_" + std::to_string(i);
        std::string actual = db.get(key);
        if (actual != expected) {
            std::cerr << "mp_reader: mismatch key=" << key << " expected=" << expected << " got=" << actual << "\n";
            ++failures;
        }
    }

    return failures == 0 ? 0 : 1;
}
