# Analysis: `concurrent_stress` test

Date: 2026-09-21
Subject: Stress test in `tests/basic_tests.cpp` (lines ~299–403) — does it exercise the
project's multithreading setup in an optimal way?

## Verdict

Yes — it is a well-designed test for this project's actual concurrency architecture,
with a few gaps worth knowing about. It is optimal as a **regression** test; as a
race **detector** it needs repetition or ThreadSanitizer to be reliable.

## What the test gets right

The implementation has three concurrency actors:

- Writers serialized by `write_mutex_` in `Database::put` / `Database::remove`
  (`src/database.cpp`).
- Readers hitting `Storage::mutex_` (`include/simple_db/storage.h`).
- The background `SnapshotScheduler` thread that calls
  `do_snapshot_and_truncate()` (which itself takes `write_mutex_`).

The test maps well onto this:

1. **The legal-value-set check is the strongest part.** Every value is a pure
   function of `(writer, op_index)` and values are unique per key, so a reader
   observing *anything* else — another writer's value, torn `std::string` bytes,
   garbage — proves `Storage::get` ran without synchronization. This catches the
   classic "removed the lock, tests still pass on a quiet machine" regression.
2. **Deterministic final state** via disjoint key spaces avoids the false-positive
   problem of the earlier `concurrent_writes` test, where any thread's value is
   legal so reordering is invisible.
3. **Post-reload convergence** verifies the WAL-append + storage-update pair
   stayed atomic under `write_mutex_`, which is the invariant `put()` actually
   guarantees.
4. **No timing flakiness** — no sleeps or polling; all assertions happen after
   `join()`. (Unlike `background_snapshot`, which polls file size.)
5. **The snapshot thread does fire**: 3 writers × 450 puts ≈ 1350 writes ≫
   `kSnapshotCountThreshold = 100`, so snapshot + rename + WAL-compact genuinely
   races with live writers and readers.

## Weaknesses / gaps

1. **Symptom-based, single-run.** A race that requires a rare interleaving may
   pass 999 times and fail once. Good regression guard, weak detector.
   Recommend: run the suite under ThreadSanitizer (clang on Windows, or a Linux
   CI leg), and/or loop the stress block a few times.
2. **Snapshot firing is implicit, not asserted.** Nothing fails if a refactor
   accidentally stops the scheduler from triggering during the stress phase
   (e.g., threshold raised above 1350). Cheap fix: check WAL file size stays
   bounded mid-run, like `background_snapshot` does.
3. **Put-only workload.** No concurrent `remove()`, and readers never probe
   missing keys mid-flight in a meaningful way. Removes only race in the
   separate `concurrent_writes` test, so coverage exists overall — just not
   under read pressure.
4. **`keys()` / `size()` not exercised concurrently.** These take the storage
   lock and copy/iterate the map; a regression there (e.g., iterating without
   the lock) would not be caught by this test.
5. **Readers can't catch WAL-side corruption directly** — only the final reload
   does. Fine given the architecture, but a WAL bug that self-heals via snapshot
   would be invisible.

## Suggested minimal improvements

- After the writer/reader join, assert a snapshot occurred during the run (e.g.,
  capture WAL size before and after, or expose a snapshot counter for tests).
- Add one reader thread calling `db.keys()` / `db.size()` in its loop to cover
  the iteration path under mutation.
- Add a TSAN build configuration to CI — that turns this from "catches
  corruption when it manifests" into "catches the data race even when output
  looks correct."
