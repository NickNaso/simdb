# SimDB API Reference

This document describes the public API exposed by `simdb.hpp`.
For setup and tutorial examples, see `README.md`.

## Error Codes

```cpp
enum class simdb_error {
  NO_ERRORS = 2,
  DIR_NOT_FOUND,
  DIR_ENTRY_ERROR,
  COULD_NOT_OPEN_MAP_FILE,
  COULD_NOT_MEMORY_MAP_FILE,
  SHARED_MEMORY_ERROR,
  FTRUNCATE_FAILURE,
  FLOCK_FAILURE,
  PATH_TOO_LONG,
  OUT_OF_SPACE
};
```

Notes:
- `NO_ERRORS` means the last relevant operation succeeded.
- `OUT_OF_SPACE` is used when block allocation fails or stream publication fails.

## Main Type

### Constructor

```cpp
simdb(const char* name, u32 blockSize, u32 blockCount, bool raw_path = false);
```

Parameters:
- `name`: Logical database name. The backing shared object is prefixed with `simdb_`.
- `blockSize`: Bytes per block.
- `blockCount`: Number of blocks.
- `raw_path`: If true, treat `name` as a raw path when supported by the platform backend.

### Lifecycle and status

```cpp
bool close();
[[nodiscard]] simdb_error error() const;
void flush() const;
```

Important:
- Move constructor and move assignment are deleted.
- Keep a `simdb` instance at a stable address while any `WriteStream` is active.

## Key/Value Operations

### Raw API

```cpp
i64  len(const void* key, u32 klen, u32* out_vlen = nullptr, u32* out_version = nullptr) const;
bool get(const void* key, u32 klen, void* out_val, u32 vlen, u32* out_readlen = nullptr) const;
bool put(const void* key, u32 klen, const void* val, u32 vlen, u32* out_startBlock = nullptr);
bool del(const void* key, u32 klen);
```

### C-string convenience

```cpp
bool get(const char* key, void* val, u32 vlen) const;
bool put(const char* key, const void* val, u32 vlen, u32* out_startBlock = nullptr);
```

### std::string convenience

```cpp
i64         len(std::string const& key, u32* out_vlen = nullptr, u32* out_version = nullptr) const;
i64         put(std::string const& key, std::string const& value);
bool        get(std::string const& key, std::string* out_value) const;
std::string get(std::string const& key) const;
bool        del(std::string const& key);
```

### Version-aware reads

```cpp
struct simdb::VerStr {
  u32 ver;
  std::string str;
};

bool        get(simdb::VerStr const& vs, std::string* out_value) const;
std::string get(simdb::VerStr const& vs) const;
```

`VerStr` is declared as a nested type inside class `simdb`.

### Vector convenience

```cpp
template<class T>
std::vector<T> get(std::string const& key);

template<class T>
i64 put(std::string const& key, std::vector<T> const& val);
```

## Iteration and Discovery

```cpp
simdb::VerStr                     nxtKey(u64* searched = nullptr) const;
std::vector<simdb::VerStr>        getKeyStrs() const;
```

Behavior notes:
- `nxtKey` iterates keys using the internal hash traversal state.
- `getKeyStrs` returns a sorted snapshot of currently discoverable keys.

## Free Functions

```cpp
[[nodiscard]] std::vector<std::string> simdb_listDBs(simdb_error* error_code = nullptr);
```

Behavior notes:
- `simdb_listDBs` is a namespace-scope helper (not a `simdb` member) and lists available SimDB instances in the OS-specific backing space.

## Streaming API

Use the streaming API for large values when you want chunked writes and zero-copy chunked reads.

### `WriteStream`

Created by `begin_write`.

```cpp
class WriteStream {
public:
  [[nodiscard]] bool valid() const noexcept;
  bool write(const void* data, u32 len) noexcept;
  bool commit(u32 committed_bytes = 0) noexcept;
  void abort() noexcept;
};
```

Semantics:
- `valid()` is false when pre-allocation failed.
- `write()` fails if you exceed the reserved max size.
- `commit()` publishes the entry atomically in the hash table.
- `abort()` frees reserved blocks without publication.

### Begin write

```cpp
[[nodiscard]] WriteStream begin_write(std::string const& key, u32 max_value_bytes);
```

Notes:
- Empty keys return an invalid stream.
- Allocation or publication failures update `error()` (typically to `OUT_OF_SPACE`).

### Read stream

```cpp
template<typename Callback>
bool read_stream(std::string const& key, Callback&& cb) const;
```

Callback contract:
- Signature must be compatible with `bool(const void*, u32)`.
- Chunk pointers are valid only during callback execution.
- Returning `false` stops iteration early.

## Introspection Helpers

```cpp
[[nodiscard]] u64  size() const;
[[nodiscard]] bool isOwner() const;
[[nodiscard]] u64  blocks() const;
[[nodiscard]] u64  blockSize() const;
[[nodiscard]] void* mem() const;
[[nodiscard]] u64  memsize() const;
[[nodiscard]] const void* data() const;
[[nodiscard]] const void* hashData() const;
[[nodiscard]] u32  cur() const;
```

These methods expose memory layout and ownership information useful for diagnostics and advanced tooling.
