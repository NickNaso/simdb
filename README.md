
![alt text](https://github.com/LiveAsynchronousVisualizedArchitecture/simdb/blob/master/numbered_slots_upshot.jpg "A key value store is kind of like this")

# SimDB
#### A high performance, shared memory, lock-free, cross platform, single file, no dependencies, C++20 key-value store.

SimDB is part of LAVA (Live Asynchronous Visualized Architecture) which is a series of single file, minimal dependency, C++20 files to create highly concurrent software while the program being written runs live with internal data visualized.

- Hash based key-value store created to be a fundamental piece of a larger software architecture. 

- High Performance - Real benchmarking needs to be done, but superficial loops seem to run *conservatively* at 500,000 small get() and put() calls per logical core per second. Because it is lock free the performance scales well while using at least a dozen threads. 

- Shared Memory - Uses shared memory maps on Windows, Linux, and OS X without relying on any external dependencies.  This makes it __exceptionally good at interprocess communication__. 

- Lock Free - The user facing functions are thread-safe and lock free with the exception of the constructor (to avoid race conditions between multiple processes creating the memory mapped file at the same time). 

- Cross Platform - Powered by CMake and tested extensively across Windows MSVC 2022, linux gcc/clang, and Apple Clang on macOS.

- Single File - simdb.hpp and the C++20 standard library is all you need. No Windows SDK or any other dependencies, not even from the parent project. 

- Apache 2.0 License - No need to GPL your whole program to include one file. 

This has already been used for both debugging and visualization, but *should be treated as alpha software*.  Though there are no known outstanding bugs, there are almost certainly bugs (and small design issues) waiting to be discovered and so will need to be fixed as they arise. 

#### Getting Started

```cpp
simdb db("test", 1024, 4096);
```

This creates a shared memory file that will be named "simdb_test". It will be a file in a temp directory on Linux and OSX and a 'section object' in the current windows session namespace (basically a temp file complicated by windows nonsense).

It will have 4096 blocks of 1024 bytes each.  It will contain about 4 megabytes of space in its blocks and the actual file will have a size of about 4MB + some overhead for the organization (though the OS won't write pages of the memory map to disk unless it is neccesary). 

```cpp
auto dbs = simdb_listDBs();
```

This will return a list of the simdb files in the temp directory as a std::vector of std::string.  Simdb files are automatically prefixed with "simdb_" and thus can searched for easily here.  This can make interprocess communication easier so that you can do things like list the available db files in a GUI.  It is here for convenience largely because of how difficult it is to list the temporary memory mapped files on windows. 

```cpp
db.put("lock free", "is the way to be");
```

SimDB works with arbitrary byte buffers for both keys and values. This example uses a convenience function to make a common case easier. 

```cpp 
string s = db.get("lock free");       // returns "is the way to be"
```

This is another convenience function for the same reason. Next will be an example of the direct functions that these wrap.

```cpp
string lf  = "lock free";

string way = "is the way to be";

i64    len = db.len( lf.data(), (u32)lf.length() );

string way2(len,'\0');

bool    ok = db.get( lf.data(), (u32)lf.length(), (void*)way.data(), (u32)way.length() );
```

Here we can see the fundamental functions used to interface with the db. An arbitrary bytes buffer is given for the key and another for the value.  Keep in mind here that get() can fail, since another thread can delete or change the key being read between the call to len() (which gets the number of bytes held in the value of the given key) and the call to get().
Not shown is del(), which will take a key and delete it.

```cpp
auto ws = db.begin_write("binary_payload", 5 * 1024 * 1024); // 5MB limit
if (ws.valid()) {
    bool ok = true;
    while(auto chunk = get_next_buffer()) {
        if (!ws.write(chunk.data(), chunk.size())) { ok = false; break; }
    }
    if (ok) {
        if (!ws.commit()) { /* handle commit failure */ }
    }
}

// Memory zero-copy readout:
db.read_stream("binary_payload", [](const void* data, uint32_t len) {
    write_to_disk_or_network(data, len); // Bypass intermediate buffers!
    return true; 
});
```
This is the **Streaming API** providing a way to handle multi-megabyte binaries without forcing them entirely into RAM up front, managing lock-free concurrency and zero-copy reads correctly.

> **Important**: `simdb` move construction and move assignment are explicitly disabled to enforce stream safety. The parent `simdb` instance must be kept at a stable address and must outlive any active `WriteStream` pointing to it.

*Inside simdb.hpp there is a more extensive explanation of the inner working and how it achieves lock free concurrency*

## API Reference
For a complete and highly detailed breakdown of all available structs, constructors, methods, keys iterators, error handling states, and utility properties exposed by the interface, please read the [API Reference](./API_REFERENCE.md).

## Building and Testing
SimDB has migrated to a modern **CMake** implementation backed by **GoogleTest**. Ensure your toolchain supports standard `C++20`.
To build the unit testing executable suite:
```bash
cmake -S . -B build
cmake --build build --config Debug -j
cd build && ctest -C Debug --output-on-failure
```
Continuous integration flows run these standards concurrently across Linux, macOS, and Windows.

## Zig Integration
SimDB fully supports adoption via the [Zig Build System](https://ziglang.org/learn/build-system/). Since it is purely a C++ header library, Zig can flawlessly propagate it into cross-compiled architectures effortlessly via `build.zig.zon`. 
To add it as a dependency, run:
```bash
zig fetch --save https://github.com/NickNaso/simdb/archive/refs/heads/main.zip
```

And expose it inside your consumer's `build.zig` like this:
```zig
const simdb_dep = b.dependency("simdb", .{});
exe.root_module.addImport("simdb", simdb_dep.module("simdb"));

// If you need the physical library binaries:
exe.linkLibrary(simdb_dep.artifact("simdb"));
```

> **Note:** Our CI pipeline automatically cross-compiles `.lib`, `.dll`, `.a`, and `.so` library artifacts for Windows, Linux, and macOS alongside the `simdb.hpp` header. You can download the latest pre-compiled platform `.zip` payloads natively from the **[GitHub Releases](https://github.com/NickNaso/simdb/releases)** page.
