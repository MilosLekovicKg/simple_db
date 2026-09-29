# Next steps

Notes on gaps and suggested next milestones, captured after adding WAL replay,
compaction, and the background snapshot scheduler.

## Known gaps

1. ~~**No `fsync`/flush guarantees.**~~ Addressed: `LogRecord::flush_to_disk`
   now takes a `DurabilityMode` and delegates to `append_file_bytes`, which
   flushes and `fsync`s (`FlushFileBuffers`/`_commit` on Windows) before
   returning when the mode is `kSync`. `WAL` appends default to `kSync`
   via `Database`'s default constructor argument; compaction copies
   retained records with `kAsync` since they are already durable. The
   CLI exposes `--durability async|sync` (default `sync`). Snapshot
   `.tmp` fsync-before-rename and directory fsync are still open (see
   suggested next steps). Covered by the `DurabilityTest` suite.
2. ~~**`put`/`remove` aren't atomic across WAL + Storage.**~~ Addressed:
   `Database` now holds a `write_mutex_` that serializes the WAL append +
   Storage update in `put`/`remove`, and is also held across
   `do_snapshot_and_truncate` so no write can slip between the snapshot and
   the WAL compaction. Covered by the `concurrent_writes` test.
3. ~~**No corruption handling.**~~ Addressed: each `LogRecord` is now
   framed as `[int32 payload_size][payload][uint32 crc32(payload)]`, and
   `WAL::replay`/`WAL::compact` stop gracefully at the first corrupt or
   torn record, keeping only the valid prefix. Covered by the
   `wal_corruption` and `wal_torn` tests.
4. **Point lookups only.** `Storage` is a hash map - no ordered
   iteration/range scans.
5. **Fixed configuration.** Snapshot thresholds (`kSnapshotCountThreshold`,
   etc. in `database.cpp`) are compile-time constants, not configurable via
   CLI/config.
6. **`docs/` design write-ups are sparse.** Worth documenting the WAL format,
   snapshot format, and recovery algorithm before adding transactions.

## Suggested next steps (roughly in order)

1. ~~**Durability**~~: done for WAL appends - `DurabilityMode::kSync` is the
   default and flushes/fsyncs each append; group commit (batched fsyncs)
   and fsyncing the snapshot `.tmp` file / containing directory before and
   after rename remain open follow-ups.
2. ~~**Corruption detection**~~: done - CRC32 per `LogRecord`, replay and
   compaction stop at the first bad record instead of trusting the file.
3. **Concurrency test coverage**: add a stress test with multiple threads
   hammering `put`/`get`/`remove` concurrently to validate the `Storage`
   mutex actually prevents data races.
4. **Transactions / atomic multi-key ops**: introduce a
   `begin_transaction`/`commit` API, backed by grouping WAL records with a
   transaction id.
5. **Range queries / ordered storage**: swap or augment
   `std::unordered_map` with an ordered index to support range scans.
6. **Documentation**: capture the WAL/snapshot format and recovery algorithm
   in `docs/`.
