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

add_executable(simdb_test
  tests/simdb_test.cpp
)
target_link_libraries(simdb_test simdb GTest::gtest GTest::gtest_main)

gtest_discover_tests(simdb_test)
