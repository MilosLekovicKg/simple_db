# Incremental Database Learning Project

This repository is a small, buildable C++ project for learning database internals step by step. It starts with a minimal in-memory key-value store and is meant to be developed by adding features incrementally, until a full-featured database management system is created.

## Prerequisites

- CMake 3.20 or newer
- A C++20 compiler (MSVC 2022 on Windows; GCC/Clang work too)
- Internet access on the first configure — GoogleTest is downloaded automatically via CMake FetchContent

## Configuring and building

```powershell
# Generate build files (also downloads GoogleTest on first run)
cmake -S . -B build

# Build everything (library, CLI, and tests)
cmake --build build --config Debug
```

This produces three targets under `build/Debug/`:

- `simple_db.lib` — the database library
- `simple_db_cli.exe` — a small command-line front end
- `simple_db_tests.exe` — the GoogleTest-based test runner

## Running tests

Tests use GoogleTest and are registered individually with CTest:

```powershell
# Run the full test suite
ctest --test-dir build -C Debug --output-on-failure

# List all tests without running them
ctest --test-dir build -C Debug -N

# Run only tests matching a name pattern
ctest --test-dir build -C Debug -R WalTest
```

You can also run the test binary directly, which gives access to GoogleTest flags:

```powershell
.\build\Debug\simple_db_tests.exe --gtest_filter=WalTest.*
```

## Working with LLMs

LLMs are used for generating code in this project.
