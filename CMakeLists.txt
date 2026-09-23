cmake_minimum_required(VERSION 3.20)
project(simple_db LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(simple_db
    src/database.cpp
    src/logrecord.cpp
    src/snapshot_scheduler.cpp
    src/storage.cpp
    src/wal.cpp
)
target_include_directories(simple_db PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)

find_package(Threads REQUIRED)
target_link_libraries(simple_db PUBLIC Threads::Threads)

target_compile_options(simple_db PRIVATE
    $<$<CXX_COMPILER_ID:MSVC>:/W4>
    $<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-Wall -Wextra -Wpedantic>
)

add_executable(simple_db_cli src/main.cpp)
target_link_libraries(simple_db_cli PRIVATE simple_db)

include(FetchContent)
FetchContent_Declare(
    googletest
    URL https://github.com/google/googletest/archive/refs/tags/v1.15.2.tar.gz
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)
# Keep gtest on the same MSVC runtime as the project and out of install rules.
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

enable_testing()
include(GoogleTest)
add_executable(simple_db_tests
    tests/concurrency_tests.cpp
    tests/database_tests.cpp
    tests/logrecord_tests.cpp
    tests/recovery_tests.cpp
    tests/storage_tests.cpp
    tests/wal_tests.cpp
)
target_include_directories(simple_db_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/tests)
target_link_libraries(simple_db_tests PRIVATE simple_db GTest::gtest_main)
gtest_discover_tests(simple_db_tests)
