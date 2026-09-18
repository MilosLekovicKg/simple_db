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

enable_testing()
add_executable(simple_db_tests tests/basic_tests.cpp)
target_link_libraries(simple_db_tests PRIVATE simple_db)
add_test(NAME basic_tests COMMAND simple_db_tests)
