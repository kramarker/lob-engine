cmake_minimum_required(VERSION 3.20)
project(lob_engine LANGUAGES CXX)

# ----------------------------------------------------------------------------
# Build configuration (scaffold). No source files exist yet — targets below are
# created conditionally so `cmake` configures cleanly before implementation.
# ----------------------------------------------------------------------------

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()

option(LOB_BUILD_TESTS      "Build unit tests (GoogleTest)"        ON)
option(LOB_BUILD_BENCHMARKS "Build microbenchmarks (Google Benchmark)" ON)
option(LOB_ENABLE_SANITIZERS "Enable ASan/UBSan in Debug builds"   OFF)

add_library(lob_warnings INTERFACE)
target_compile_options(lob_warnings INTERFACE
  $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall -Wextra -Wpedantic -Wconversion>)

if(LOB_ENABLE_SANITIZERS)
  target_compile_options(lob_warnings INTERFACE -fsanitize=address,undefined -fno-omit-frame-pointer)
  target_link_options(lob_warnings INTERFACE -fsanitize=address,undefined)
endif()

# Core library — only declared once src/*.cpp exists.
file(GLOB LOB_SOURCES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp)
if(LOB_SOURCES)
  add_library(lob_engine ${LOB_SOURCES})
  target_include_directories(lob_engine PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
  target_link_libraries(lob_engine PRIVATE lob_warnings)
else()
  message(STATUS "lob-engine: no sources in src/ yet — core library target skipped.")
endif()

# Tests — pulled in via FetchContent once test sources exist.
file(GLOB LOB_TEST_SOURCES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/tests/*.cpp)
if(LOB_BUILD_TESTS AND LOB_TEST_SOURCES AND LOB_SOURCES)
  include(FetchContent)
  FetchContent_Declare(googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG v1.15.2)
  FetchContent_MakeAvailable(googletest)
  enable_testing()
  add_executable(lob_tests ${LOB_TEST_SOURCES})
  target_link_libraries(lob_tests PRIVATE lob_engine lob_warnings GTest::gtest_main)
  include(GoogleTest)
  gtest_discover_tests(lob_tests)
endif()

# Benchmarks — pulled in via FetchContent once benchmark sources exist.
file(GLOB LOB_BENCH_SOURCES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/benchmarks/*.cpp)
if(LOB_BUILD_BENCHMARKS AND LOB_BENCH_SOURCES AND LOB_SOURCES)
  include(FetchContent)
  set(BENCHMARK_ENABLE_TESTING OFF CACHE BOOL "" FORCE)
  FetchContent_Declare(googlebenchmark
    GIT_REPOSITORY https://github.com/google/benchmark.git
    GIT_TAG v1.9.0)
  FetchContent_MakeAvailable(googlebenchmark)
  add_executable(lob_benchmarks ${LOB_BENCH_SOURCES})
  target_link_libraries(lob_benchmarks PRIVATE lob_engine lob_warnings benchmark::benchmark)
endif()
