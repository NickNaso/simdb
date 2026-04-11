cmake_minimum_required(VERSION 3.20)
project(simdb LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_library(simdb INTERFACE)
target_include_directories(simdb INTERFACE ${CMAKE_CURRENT_SOURCE_DIR})

include(FetchContent)
set(GTEST_VERSION v1.14.0)
FetchContent_Declare(
  googletest
  URL https://github.com/google/googletest/archive/refs/tags/${GTEST_VERSION}.zip
)
FetchContent_MakeAvailable(googletest)

enable_testing()
include(GoogleTest)

# ---------------------------------------------------------------------------
# Unit / stream tests  (single-process)
# ---------------------------------------------------------------------------
add_executable(simdb_test
  tests/simdb_test.cpp
  tests/simdb_stream_test.cpp
)
target_link_libraries(simdb_test simdb GTest::gtest GTest::gtest_main)

gtest_discover_tests(simdb_test)

# ---------------------------------------------------------------------------
# Helper executables used by the multi-process integration tests.
# These are small standalone programs that are spawned as child processes.
# ---------------------------------------------------------------------------
add_executable(simdb_mp_writer
  tests/helpers/mp_writer.cpp
)
target_link_libraries(simdb_mp_writer simdb)

add_executable(simdb_mp_reader
  tests/helpers/mp_reader.cpp
)
target_link_libraries(simdb_mp_reader simdb)

add_executable(simdb_mp_concurrent_writer
  tests/helpers/mp_concurrent_writer.cpp
)
target_link_libraries(simdb_mp_concurrent_writer simdb)

# ---------------------------------------------------------------------------
# Multi-process integration test binary
#
# $<TARGET_FILE:tgt> expands to the full, platform-correct absolute path of
# the executable (including .exe on Windows and the correct Debug/Release
# subdirectory for multi-config generators).  We inject these paths as
# compile-time string literals so the test binary can locate helpers at
# runtime without any hard-coded paths.
# ---------------------------------------------------------------------------
add_executable(simdb_multiprocess_test
  tests/simdb_multiprocess_test.cpp
)
target_link_libraries(simdb_multiprocess_test simdb GTest::gtest GTest::gtest_main)
target_compile_definitions(simdb_multiprocess_test PRIVATE
  SIMDB_MP_WRITER_PATH="$<TARGET_FILE:simdb_mp_writer>"
  SIMDB_MP_READER_PATH="$<TARGET_FILE:simdb_mp_reader>"
  SIMDB_MP_CONC_WRITER_PATH="$<TARGET_FILE:simdb_mp_concurrent_writer>"
)

# Ensure helper binaries are built before the integration test binary.
add_dependencies(simdb_multiprocess_test
  simdb_mp_writer
  simdb_mp_reader
  simdb_mp_concurrent_writer
)

gtest_discover_tests(simdb_multiprocess_test
  PROPERTIES LABELS "multiprocess"
)
