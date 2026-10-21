# Roadmap

Goal: build a complete database management system incrementally, learning one
core concept per milestone. The order below follows the classic DBMS layering
(storage -> access methods -> recovery/transactions -> relational layer ->
query processing -> server), not "whatever is next on the TODO list".

## Guiding principles

1. **One new idea per milestone.** Each milestone names the concept it teaches
   and ends with an observable exit criterion (tests and/or a CLI demo).
2. **Always runnable.** The CLI and test suite pass after every milestone; no
   long-lived rewrite branches.
3. **Simple first, then real.** Introduce a simple version of a component
   (in-memory, single-threaded, logical logging), keep it as a reference/test
   oracle, then replace it with the real one (pages, latches, physical logging).
4. **Interfaces before implementations.** Cut a narrow interface (`Engine`,
   `Index`, `Executor`) so implementations can be swapped and differentially
   tested against each other.
5. **Prove it, don't trust it.** Every durability/concurrency claim gets a
   test: crash injection, differential testing against the reference
   implementation, ThreadSanitizer, fuzzing of on-disk formats.
6. **Write the design note as you go.** Each milestone adds a short document to
   `docs/` (format, invariants, alternatives rejected) instead of one big
   documentation task at the end.

## Where we are (done)

A single-process, in-memory key-value store with a durable log:

- `Storage`: `unordered_map` behind a mutex; snapshot (de)serialization.
- WAL: framed records `[size][payload][crc32]`, per-append `fsync`
  (`DurabilityMode`), replay and compaction that stop at the first torn or
  corrupt record.
- `Database`: write mutex making WAL append + storage update atomic; recovery =
  load snapshot, replay WAL records with `lsn > snapshot lsn`; background
  snapshot scheduler (count/bytes/time thresholds).
- CLI (`put/get/delete/list`) and a GoogleTest suite (durability, corruption,
  torn writes, recovery, concurrency).

Concepts already covered: write-ahead logging, LSNs, checksums, fsync,
checkpoint + log truncation, crash recovery, basic mutual exclusion.

Known limitations of the current design (each is addressed below):

- Whole dataset must fit in RAM; every checkpoint rewrites everything.
- Only point lookups; no ordered access.
- Logging is logical and per-operation; no multi-key atomicity.
- Snapshot `.tmp` file and directory are not fsynced; no group commit.
- Single key space, untyped string values, no query language.

## Phase 1 - Foundations and hygiene (small, do first)

Cheap work that de-risks everything after it.

- **M1.1 Concurrency test coverage.** **Done.** The stress test now exercises
  concurrent `remove`, `keys`, `size` and missing-key reads; CMake has an
  opt-in ThreadSanitizer build configuration (see `stress-test-analysis.md`).
  *Learn:* how to actually detect data races.
- **M1.2 Crash-consistency hardening.** **Done.** Snapshot and WAL replacement
  now sync the temporary file before replacement and use durable replacement
  semantics (`fsync` of the containing directory on POSIX and
  `MOVEFILE_WRITE_THROUGH` on Windows). `durable_file` has a deterministic
  fault-injection hook for torn writes and sync failures, covered by tests.
  *Learn:* what "durable" really requires on POSIX/Windows.
- **M1.3 Configuration.** **Done.** `Options` (durability and snapshot
  count/byte/interval thresholds, validated on construction) is passed to
  `Database` and wired to CLI flags, replacing the compile-time constants.
  Page size and buffer pool size will be added to `Options` when those
  milestones land.
- **M1.4 Design note: current WAL/snapshot format and recovery algorithm.**
  Write it now, while it is small; it becomes the baseline to contrast with the
  later physical log.

Exit: TSan clean, fault-injection suite green, `Options` in place.

## Phase 2 - Ordered access and atomic batches (in-memory)

- **M2.1 Ordered storage and range scans.** **Done.** *Learn:* ordered indexes
  and the scan API contract.
  - Introduce an `Engine` interface: `put/get/remove`, `scan(start, end, limit)`
    with half-open `[start, end)`, bytewise key order, results copied under the
    lock (no dangling iterators).
  - Reference implementation: replace the hash map with `std::map` (with a
    transparent comparator). Snapshot format is unchanged.
  - Add `scan_prefix`, CLI `range`/`prefix`, and paging via `start_after`.
  - **This `std::map` engine is deliberately a stepping stone.** It becomes the
    *test oracle* for the B+tree in Phase 3 (differential/randomized tests) and
    defines the API the SQL layer will call. It is not the final index.
- **M2.2 Atomic write batches.** *Learn:* commit records and the all-or-nothing
  recovery rule.
  - `WriteBatch` applied atomically: WAL gets `Begin/Put/Remove/Commit`
    framing (or a single multi-op record); recovery ignores batches without a
    commit marker.
  - This is the "simple" transaction (no isolation, no rollback API). It is
    intentionally superseded by M4.x but teaches the recovery rule first.

Exit: range/prefix/paging tests, randomized comparison against a trivial model,
crash tests proving batches are all-or-nothing.

## Phase 3 - Disk-based storage engine

The biggest conceptual jump: stop assuming the data fits in memory.

- **M3.1 Pages and disk manager.** Fixed-size pages (e.g. 4 KiB), page ids,
  file header, free-page list, page checksums. `DiskManager` does
  read/write/allocate/deallocate page. *Learn:* the page as the unit of I/O.
- **M3.2 Buffer pool.** Frames, page table, pin/unpin, dirty flags, eviction
  (LRU, then clock/LRU-K), page latches. *Learn:* caching, replacement
  policies, why the OS page cache is not enough. Bonus: hit-rate metrics.
- **M3.3 Slotted pages / variable-length records.** Slot directory, insert,
  delete, update, compaction, overflow pages for large values.
- **M3.4 Persistent B+tree.** Variable-length keys, search, insert with split,
  delete with merge/redistribute, leaf sibling links for range scans, cursor
  API. Validate with an `check_invariants()` routine and differential tests
  against the M2.1 `std::map` engine. *Learn:* the index that powers range
  queries.
- **M3.5 Storage engine integration.** `BTreeEngine` implements `Engine`;
  `Database` can run on either engine (`--engine memory|btree`). Checkpointing
  becomes "flush dirty pages", replacing whole-file snapshots.
- **M3.6 B+tree concurrency.** Start with a tree-level reader/writer lock, then
  latch crabbing (and optionally optimistic latching). *Learn:* latches vs
  locks, deadlock-free latch ordering.

Interim durability note: until Phase 4, the engine is made recoverable by
keeping the logical WAL and replaying it onto the last flushed state; torn
page writes are covered by checksums plus a full rewrite from the log.
Phase 4 replaces this with the proper mechanism.

Exit: dataset larger than the buffer pool works; randomized B+tree ops match
the oracle; benchmarks (insert, point get, scan) recorded for both engines.

## Phase 4 - Recovery and transactions

- **M4.1 Physiological WAL and ARIES-style recovery.** Log records carry page
  id + redo/undo info; each page stores a `page_lsn`; WAL-before-data rule is
  enforced by the buffer pool (flush log up to `page_lsn` before writing a
  page); fuzzy checkpoints; analysis/redo/undo recovery; compensation log
  records. *Learn:* steal/no-force buffer management and why ARIES works.
  Add group commit here (batched fsync).
- **M4.2 Transaction manager.** `begin/commit/abort`, transaction ids, undo
  chains, rollback via the log. Autocommit for the CLI.
- **M4.3 Concurrency control.** Pick one and implement it fully, then compare:
  - strict two-phase locking with a lock manager (shared/exclusive, intention
    locks later), deadlock detection/timeouts, isolation levels; or
  - MVCC with snapshot isolation (version chains, garbage collection).
  Recommended order: 2PL first (simpler to reason about), MVCC as a stretch.
  *Learn:* serializability, anomalies (dirty/non-repeatable/phantom reads,
  write skew), and how range scans interact with locking (phantoms -> key-range
  or predicate locks).
- **M4.4 Isolation test harness.** Deterministic interleaving tests that
  reproduce each anomaly and show which isolation level forbids it.

Exit: kill -9 at random points never loses a committed transaction or exposes
an uncommitted one; anomaly tests behave per isolation level.

## Phase 5 - Relational layer

- **M5.1 Types and row format.** `INT`, `BIGINT`, `DOUBLE`, `BOOL`, `TEXT`,
  `NULL`; tuple serialization; **order-preserving key encoding** (so B+tree
  byte order equals SQL order, including composite keys).
- **M5.2 Catalog.** Tables, columns, primary key, stored in system tables and
  versioned through the same WAL; bootstrap on first open.
- **M5.3 Tables and indexes.** Table = primary B+tree (clustered) or heap file
  + index; secondary indexes maintained transactionally; unique constraints.
- **M5.4 Programmatic table API.** Create/insert/update/delete/scan with
  predicates before SQL exists, so the layer is testable on its own.

## Phase 6 - Query processing

- **M6.1 SQL front end.** Lexer, recursive-descent parser, AST for
  `CREATE TABLE`, `INSERT`, `SELECT` (WHERE, ORDER BY, LIMIT), `UPDATE`,
  `DELETE`, `BEGIN/COMMIT/ROLLBACK`. Binder/semantic analysis against the
  catalog.
- **M6.2 Execution engine.** Volcano (iterator) model: sequential scan, index
  scan (uses range scans from M2.1/M3.4), filter, projection, sort, limit,
  insert/update/delete operators.
- **M6.3 Joins and aggregation.** Nested-loop join, hash join, (optionally
  sort-merge); `GROUP BY` with hash aggregation; external sort when data
  exceeds memory.
- **M6.4 Optimizer.** Rule-based first (predicate pushdown, index selection),
  then table statistics and a cost model with join ordering; `EXPLAIN`.
  *Learn:* why the same query can differ by orders of magnitude.

## Phase 7 - Server and operations

- **M7.1 Network server.** Simple framed protocol over TCP, multiple sessions,
  per-session transactions, thread pool; then a minimal client library.
- **M7.2 Operability.** Metrics (buffer hit rate, WAL bytes, lock waits),
  logging, online backup / point-in-time restore from the WAL, `VACUUM`/space
  reclamation, consistency checker tool.
- **M7.3 Benchmarks.** A repeatable benchmark suite (YCSB-like KV workload,
  TPC-B/TPC-C-lite) tracked across milestones to see the effect of each change.

## Phase 8 - Stretch / alternative tracks

Pick by interest once Phase 6 is done; each is a self-contained study.

- **LSM-tree engine** (memtable = the M2.1 ordered map, SSTables, compaction,
  bloom filters) as a second `Engine` to compare against the B+tree.
- **MVCC and snapshot isolation** if not done in M4.3.
- **Replication** (log shipping, then consensus), **columnar storage / vectorized
  execution**, **query-plan cache / prepared statements**, **full-text or
  geospatial index**.

## Dependency overview

```
M1 hygiene -> M2.1 ordered API ------------------+
                 |                                |
                 +-> M2.2 batches                 v
                 |                         M3.x pages/buffer/B+tree
                 |                                |
                 |                                v
                 +----------------------> M4.x ARIES + transactions
                                                  |
                                                  v
                          M5.x types/catalog/tables -> M6.x SQL + executor
                                                  |
                                                  v
                                           M7.x server + ops
```

M2.1 is the pivot: it defines the scan contract that B+tree cursors, SQL index
scans and range locking all build on, and its `std::map` engine is the oracle
that makes the Phase 3 rewrite safe.

## Cross-cutting practices

- **Testing ladder:** unit tests -> randomized differential tests against the
  simplest correct implementation -> crash/fault injection -> concurrency
  stress under TSan -> fuzzing of file formats (WAL, pages, SQL parser).
- **Format versioning:** every on-disk file starts with a magic number and
  version; add upgrade/refuse-to-open behaviour early.
- **Decision log:** record non-obvious choices (e.g. B+tree over LSM first,
  2PL before MVCC, bytewise key order) in `docs/decisions.md` with the
  alternatives considered.
- **Scope control:** a milestone is done when its exit criterion passes; resist
  adding features from later milestones.

## Open decisions to revisit

- **B+tree vs LSM as the primary engine.** This roadmap picks B+tree first
  (classic page/buffer-pool/ARIES curriculum, in-place updates, good range
  scans) and treats LSM as a Phase 8 comparison. Swap if write-heavy/log-
  structured designs are the more interesting learning target.
- **Clustered B+tree vs heap + indexes** for tables (M5.3).
- **2PL vs MVCC** as the first concurrency-control scheme (M4.3).
