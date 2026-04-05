# SimDB API Reference

This document serves as the formal technical Reference for the `simdb` C++ library. For guides on how to use these interfaces or build the code, please refer to the main `README.md`.

## Data Structures

### `simdb::VerStr`
A lightweight struct binding a string payload to its block progression version. Used extensively to guarantee consistent lock-free iteration and reads.
```cpp
struct VerStr { 
  u32 ver; 
  std::string str; 
};
```

### `simdb_error`
An enumerator defining standard failure states you might encounter when attempting to instantiate the map or assign boundaries that exceed hardware sizes.
- `NO_ERRORS = 2`: Initialization and operations succeeded.
- `DIR_NOT_FOUND`: Internal OS failure traversing shared/tmp spaces.
- `DIR_ENTRY_ERROR`: Internal OS failure listing files.
- `COULD_NOT_OPEN_MAP_FILE`: OS denied map creation.
- `COULD_NOT_MEMORY_MAP_FILE`: Paging creation failed natively.
- `SHARED_MEMORY_ERROR`: Generic shared memory handle fault.
- `FTRUNCATE_FAILURE` / `FLOCK_FAILURE`: Memory resizing boundaries failed.
- `OUT_OF_SPACE`: The configured block allocations limits (`blockCount`) is entirely exhausted by users or previous keys.

---

## Constants
- **`simdb::EMPTY`**: Represents a functionally empty or unused slot.
- **`simdb::DELETED`**: Represents a tombstone index marking deleted elements inside the hash map.
- **`simdb::LIST_END`**: The marker designating the end of a contiguous concurrent block sequence.

---

## Core Operations

### `simdb()` (*Constructor*)
Open or create a new concurrent memory mapped file utilizing the shared space for inter-process communications.
```cpp
simdb(const char* name, u32 blockSize, u32 blockCount, bool raw_path=false);
```
- **name**: Name of the database pool to generate/access (e.g. `"my_data_pool"` prefixing `simdb_my_data_pool`).
- **blockSize**: Allocation granularity per partition block (e.g., `4096`).
- **blockCount**: Number of available blocks in the map pool. Pre-allocates sizing instantly.
- **raw_path**: Whether the system expects absolute path references over standard temporal pools.

### `error()`
Validates if initialization or specific structural commands operated successfully. Check specifically against `simdb_error::OUT_OF_SPACE` post-initialization.
```cpp
[[nodiscard]] simdb_error error() const;
```

---

## Key/Value Manipulation

> **Note**: For peak efficiency across pure C/C++ memory blocks, `simdb` exposes identical signatures accepting raw `void*` alongside modernized `std::string` and `std::vector<T>` generic overloads.

### `put(...)`
Atomically allocate block lists for a specified key and safely stream the value data array inside. Does not guarantee atomicity over the *contents* byte streaming, but absolutely locks out conflicting overwrites structurally or concurrent mapping overlaps.
```cpp
// String convenience
i64  put(str const& key, str const& value);

// Vector convenience
template<class T>
i64  put(str const& key, std::vector<T> const& val);

// Raw pointer implementation
bool put(const void* const key, u32 klen, const void* const val, u32 vlen, u32* out_startBlock=nullptr);
```
**Returns**: `false` on collision logic exhaustion or explicitly if `OUT_OF_SPACE`.

### `get(...)`
Validates key match bounds and reads the sequential inner sequences into pre-allocated outputs natively. 
```cpp
// String implementations
std::string get(std::string const& key) const;
bool        get(std::string const& key, std::string* out_value) const;

// Struct-Safe Validated Get
bool        get(VerStr const& vs, std::string* out_value) const;

// Typesafe Generic Vector Implementations
template<class T> 
std::vector<T> get(std::string const& key);

// Raw Pointer Implementations
bool        get(const void* const key, u32 klen, void* const out_val, u32 vlen, u32* out_readlen=nullptr) const;
```
**Notes**: The `VerStr` overload bypasses heavy map hashes entirely and is recommended if iterating bulk values continuously.

### `del(...)`
Deletes the key hash map pointer tracking to flag the underlying memory chain up for cyclic lock-free reuse.
```cpp
bool del(std::string const& key);
bool del(const void* const key, u32 klen);
```

### `len(...)`
Retrieve the current byte-width stored in relation to the specified memory key index to provision strings sizes prior to parsing.
```cpp
i64 len(std::string const& key, u32* out_vlen=nullptr, u32* out_version=nullptr) const;
i64 len(const void* const key, u32 klen, u32* out_vlen=nullptr, u32* out_version=nullptr) const;
```

---

## Streaming Operations

To support extremely large values, such as images or continuous byte bursts, without exhausting application memory, `simdb` offers a low-level chunk-based streaming API.

### `simdb::WriteStream` (RAII)
Handles writing payload sequences sequentially. Obtain this structured handle via `begin_write`.
- **`valid()`**: `bool` – Validates if the internal map had enough overall capacity to allocate your stream.
- **`write(const void*, u32)`**: `bool` – Copies chunks directly into the memory blocks. Returns `false` if exceeding the `max_value_bytes` supplied to `begin_write`.
- **`commit(u32 committed_bytes = 0)`**: `bool` – Makes the fully populated data visibly atomic inside the Hash table. If you pass `committed_bytes` less than your initial request, the excess blocks are returned to the pool efficiently.
- **`abort()`**: `void` – Trashes the pre-allocated structures without exposing them to other processes. Triggers automatically on destruction if `commit` wasn't invoked.

### `begin_write(...)`
Atomically configure space for a contiguous stream sequence without applying table access constraints. Data written remains strictly invisible cross-process until explicitly committed. Be careful to check `valid()` before writing. 
```cpp
[[nodiscard]] WriteStream begin_write(str const& key, u32 max_value_bytes);
```

### `read_stream(...)`
Extracts payloads dynamically as zero-copy chunks utilizing an injection callback. Recommended strictly for immediate consumption pipelines like disk writes or networking outputs where you want to minimize `simdb` memory cloning. Iteration will cleanly bail if your callback evaluates to `false`.

> **Note**: The `chunk` pointers provided to the callback point directly into shared map memory and are ONLY valid during the callback execution. Storing or using them after the callback returns will result in dangling pointers and invalid access.

```cpp
template<typename Callback>
bool read_stream(str const& key, Callback&& cb) const;
// Expected Callback: bool(const void* chunk, uint32_t len)
```

---

## Iterators and Utility

### `getKeyStrs()`
Resolves and aggregates a snapshot of every populated structured key representation existing inside the memory.
```cpp
std::vector<VerStr> getKeyStrs() const;
```

### `nxtKey()`
Iterates directly forward on the linked sequences. Extremely rapid lookup bypass to avoid hashing over sequential fetches.
```cpp
VerStr nxtKey(u64* searched=nullptr) const;
```

### `simdb_listDBs()`
Lists all the available `simdb` temporal memory spaces in the running operating system's temporary file locations. Useful for launching independent visualizations relying on automatic IPC detection.
```cpp
[[nodiscard]] std::vector<std::string> simdb_listDBs(simdb_error* error_code=nullptr);
```

### Structural Introspection getters
Direct mappings onto the internal sizes and lock behaviors useful for diagnostics and memory safety testing scenarios.
- `[[nodiscard]] u64 size() const`: Returns physical total byte mapping footprint configuration.
- `[[nodiscard]] u64 blocks() const`: Returns total discrete allocation `BlockCount`.
- `[[nodiscard]] u64 blockSize() const`: Returns static `BlockSize` configuration definitions.
- `[[nodiscard]] void* mem() const`: Initial memory alignment offset pointing to native handles.
- `[[nodiscard]] void* data() const`: Internal C-style array starting point indicating standard contiguous keys mappings payload.
- `[[nodiscard]] bool isOwner() const`: Returns `true` if the local instantiating process mapped the allocation first inside the current session.
