![SimDB key-value store architecture diagram](./simdb.png "A key value store is kind of like this")

# SimDB

[![CI](https://github.com/NickNaso/simdb/actions/workflows/ci.yml/badge.svg)](https://github.com/NickNaso/simdb/actions/workflows/ci.yml)
[![Cross-Compile Zig Release](https://github.com/NickNaso/simdb/actions/workflows/zig-release.yml/badge.svg)](https://github.com/NickNaso/simdb/actions/workflows/zig-release.yml)

High-performance, shared-memory, lock-free, cross-platform key-value store for C++20.

Note: this project is based on the original work from [LiveAsynchronousVisualizedArchitecture/simdb](https://github.com/LiveAsynchronousVisualizedArchitecture/simdb).

## Features

- Shared memory map on Windows, Linux, and macOS for fast inter-process communication.
- Lock-free reads/writes/deletes on the user-facing API (constructor excluded).
- Single-header public API (`simdb.hpp`) with no external runtime dependency.
- Cross-platform build and tests with CMake and GoogleTest.
- Streaming API for large payloads, with zero-copy chunked read callbacks.

SimDB is already used in real scenarios, but should still be treated as alpha software while the API and edge cases continue to mature.

## Import and Use in a C++ Program

### 1. Include the header

```cpp
#include "simdb.hpp"
```

### 2. Build setup (CMake)

If SimDB is a subfolder in your project:

```cmake
add_subdirectory(path/to/simdb)

add_executable(my_app main.cpp)
target_link_libraries(my_app PRIVATE simdb)
target_include_directories(my_app PRIVATE path/to/simdb/include)
```

The `simdb` target is an INTERFACE target that exports the include directory.

### 3. Simple complete example

```cpp
#include <cstdint>
#include <iostream>
#include <string>
#include "simdb.hpp"

int main() {
    simdb db("example", 4096, 4096);
    if (db.error() != simdb_error::NO_ERRORS) {
        std::cerr << "Failed to open/create SimDB instance\n";
        return 1;
    }

    if (!db.put("greeting", static_cast<const void*>("hello"), 5)) {
        std::cerr << "put() failed\n";
        return 1;
    }

    std::uint32_t value_len = 0;
    if (db.len("greeting", &value_len) <= 0) {
        std::cerr << "len() failed or key not found\n";
        return 1;
    }

    std::string value(value_len, '\0');
    if (!db.get("greeting", value.data(), value_len)) {
        std::cerr << "get() failed\n";
        return 1;
    }

    std::cout << "greeting = " << value << "\n";
    return 0;
}
```

Alternative convenience APIs are also available:

```cpp
db.put(std::string("key"), std::string("value"));
std::string value = db.get(std::string("key"));
```

## Getting Started Notes

```cpp
simdb db("test", 1024, 4096);
```

This creates or opens a DB named `simdb_test`.

- On Linux/macOS it is backed by a temporary file mapping.
- On Windows it is backed by a named shared section object.

With `blockSize=1024` and `blockCount=4096`, the data region is about 4 MiB plus metadata overhead.

List currently visible SimDB instances:

```cpp
auto dbs = simdb_listDBs();
```

## Streaming API (Large Values)

```cpp
auto ws = db.begin_write("binary_payload", 5 * 1024 * 1024);
if (ws.valid()) {
    bool ok = true;
    while (auto chunk = get_next_buffer()) {
        if (!ws.write(chunk.data(), static_cast<uint32_t>(chunk.size()))) {
            ok = false;
            break;
        }
    }

    if (ok && !ws.commit()) {
        // Handle publication failure (for example, hash table saturation)
    }
}

db.read_stream("binary_payload", [](const void* data, uint32_t len) {
    write_to_disk_or_network(data, len);
    return true;
});
```

Important: `simdb` move construction and move assignment are explicitly deleted for stream safety. Keep the parent `simdb` object at a stable address while `WriteStream` objects are alive.

## API Reference

For a complete interface reference, see [API_REFERENCE.md](./API_REFERENCE.md).

## Build and Test This Repository

Requires a C++20-capable toolchain.

```bash
cmake -S . -B build
cmake --build build --config Debug
ctest --test-dir build -C Debug --output-on-failure
```

## Zig Integration

SimDB also provides Zig build integration. Add the dependency:

```bash
zig fetch --save https://github.com/NickNaso/simdb/archive/refs/heads/main.zip
```

Use it from your `build.zig`:

```zig
const simdb_dep = b.dependency("simdb", .{});
exe.root_module.addImport("simdb", simdb_dep.module("simdb"));

// Optional: link prebuilt/static artifact if required by your target setup.
// exe.linkLibrary(simdb_dep.artifact("simdb"));
```

Prebuilt release artifacts (`.lib`, `.dll`, `.a`, `.so`) are available on [GitHub Releases](https://github.com/NickNaso/simdb/releases).
