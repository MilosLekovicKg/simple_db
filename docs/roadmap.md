# Next steps

Notes on gaps and suggested next milestones, captured after adding WAL replay,
compaction, and the background snapshot scheduler.

## Known gaps

1. **No `fsync`/flush guarantees.** `LogRecord::flush_to_disk` opens in append
   mode but never calls `flush()`/`fsync()` before returning. On a real crash
   (not just process exit), OS buffering could still lose "durable" writes.
2. **`put`/`remove` aren't atomic across WAL + Storage.** A crash between
   `wal_->append_to_wal(...)` and `storage_->put(...)` is fine (replay covers
   it), but concurrent callers could observe a WAL-only state mid-call if
   `Database` methods are ever called concurrently from multiple threads.
3. **No corruption handling.** `LogRecord::deserialize`/`WAL::replay` assume
   well-formed bytes; a torn write (crash mid-`flush_to_disk`) would likely
   throw mid-read or read garbage rather than being detected/skipped.
4. **Point lookups only.** `Storage` is a hash map - no ordered
   iteration/range scans.
5. **Fixed configuration.** Snapshot thresholds (`kSnapshotCountThreshold`,
   etc. in `database.cpp`) are compile-time constants, not configurable via
   CLI/config.
6. **`docs/` design write-ups are sparse.** Worth documenting the WAL format,
   snapshot format, and recovery algorithm before adding transactions.

## Suggested next steps (roughly in order)

1. **Durability**: add explicit flush/fsync to WAL appends; explore the
   sync-write vs. batched-write tradeoff ("group commit").
2. **Corruption detection**: add a checksum (e.g. CRC32) to `LogRecord`, and
   have `WAL::replay` stop gracefully at the first bad record instead of
   trusting the file.
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
