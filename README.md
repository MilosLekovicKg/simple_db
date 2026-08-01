# Incremental Database Learning Project

This repository is a small, buildable C++ project for learning database internals step by step. It starts with a minimal in-memory key-value store and is designed so you can add features incrementally while using LLMs as coding partners.

## Current milestone

The first complete milestone is a tiny command-line database with:

- `put <key> <value>` to insert data
- `get <key>` to retrieve data
- `delete <key>` to remove data
- `list` to show all keys

This gives you a simple working system that is easy to reason about before moving into storage layout, indexing, and persistence.

## Project layout

- `include/simple_db/` — public headers
- `src/` — implementation and CLI entry point
- `tests/` — basic regression tests
- `docs/` — notes for roadmap and LLM collaboration

## Build and test

From the project root:

```bash
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
```

## Suggested learning roadmap

1. In-memory key-value store
2. Basic command parsing and CLI ergonomics
3. Persistent append-only log
4. Simple page-based storage
5. B-tree or LSM-style indexing
6. Concurrency and transactions

## Working with LLMs

Use this project as a conversation scaffold:

- Ask for one small feature at a time.
- Require tests before accepting a change.
- Keep each step focused on one internal concept.
- Prefer explicit design discussions before implementation.

A good prompt looks like:

> Add a small feature to the in-memory database, write a regression test, and explain the design trade-off.
