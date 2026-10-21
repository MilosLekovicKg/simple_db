# WAL and snapshot format, and the recovery algorithm

Status: describes the code as of milestone M2.2 (atomic write batches). This is
the baseline to contrast with the physiological log planned for M4.1.

## Files

For a database at path `P`:

| File         | Purpose                                                       |
|--------------|---------------------------------------------------------------|
| `P`          | Snapshot: the full key/value state as of some LSN.            |
| `P.wal`      | Write-ahead log: operations after (and possibly before) the snapshot. |
| `P.tmp`      | Snapshot being written; atomically renamed over `P`.          |
| `P.wal.tmp`  | Compacted WAL being written; atomically renamed over `P.wal`. |

Leftover `.tmp` files are harmless: they are truncated and rewritten the next
time they are needed and are never read during recovery.

## Encoding conventions

- Integers are written in the host's native byte order (little-endian on all
  supported targets). Files are not portable to big-endian machines.
- `string` = `int32 length` followed by that many raw bytes. Keys and values
  are arbitrary bytes.
- CRC-32 is the IEEE 802.3 polynomial (`0xEDB88320`, reflected), see
  `crc32` in `utils.h`.

## WAL record format

Logical logging: one record per operation. A standalone `put` or `remove` is
a single self-committing record; a `WriteBatch` is framed by `Begin` and
`Commit` records (see "Atomic batches").

```
record  := int32 payload_size | payload | uint32 crc32(payload)
payload := int32 type_size (=4) | int32 type | uint64 lsn | string key | string value
type    := 0 (Put) | 1 (Remove) | 2 (Begin) | 3 (Commit)
```

- `remove`, `Begin` and `Commit` records carry empty key/value fields (a
  `remove` still has its key).
- The CRC covers the payload only. A damaged `payload_size` is caught by
  the 64 MiB sanity cap (`kMaxRecordPayloadSize`) or, failing that, by the CRC
  of the wrongly sized payload or by hitting EOF.
- A record is rejected (treated as torn/corrupt) if the size is negative or
  above the cap, the bytes are truncated, the CRC mismatches, the payload
  cannot be parsed, or the type is not one of the values above.
- Each record is appended with a single `append_file_bytes` call. With
  `DurabilityMode::kSync` it is flushed and `fsync`ed before `append_to_wal`
  returns, so an acknowledged write is on stable storage.

### Atomic batches

`Database::write(WriteBatch)` appends

```
Begin | op_1 | ... | op_n | Commit      (n >= 1, consecutive LSNs, in one write)
```

in a single `append_file_bytes` call, so sync mode pays one `fsync` for the
whole batch. The recovery rule is: **operations after a `Begin` take effect
only if the matching `Commit` record is read.** A crash can leave any prefix
of the batch's bytes on disk (including one that ends exactly on a record
boundary); every such prefix lacks the `Commit`, so the batch is discarded as a
whole. The `Commit` record is the commit point: once it is durable the batch is
recovered in full.

There is no isolation or rollback API: the batch is applied to memory under a
single storage lock (`Engine::apply`), so concurrent readers see the state
before or after the batch, but a batch cannot be built up against reads or
aborted after `write` begins. M4.x supersedes this with real transactions.

### LSNs

- Every record gets a monotonically increasing LSN (log sequence number),
  starting at 1, assigned under the WAL mutex at append time.
- `WAL::current_lsn()` is the LSN of the last record handed out.
- On open, the next LSN is `max(snapshot LSN, highest LSN found in the WAL) + 1`.
  The snapshot LSN matters because a clean shutdown leaves an empty WAL;
  without it, new records would reuse LSNs already covered by the snapshot and
  recovery would discard them.

## Snapshot format

```
snapshot := uint64 last_lsn | int32 count | count * (string key | string value)
```

- `last_lsn` is the LSN of the last WAL record whose effect is included.
- Entries are written in ascending bytewise key order (the in-memory
  `std::map` order), though loading does not depend on that.
- There is no magic number, version or checksum yet (see limitations).

## Write path

`Database::put` / `remove` / `write` hold `write_mutex_` for the whole operation:

1. Append the record (or, for `write`, the whole `Begin..Commit` group) to the
   WAL: assigns LSNs, fsyncs once in sync mode.
2. Apply the change to the in-memory `Storage` (`write` applies all operations
   under one storage lock).
3. Tell the snapshot scheduler how many bytes and operations were written.

Because the WAL append precedes the in-memory update and both happen under one
mutex, the in-memory state is always the result of applying a prefix of the
durable log. If the process dies between steps 1 and 2 the operation is
recovered from the log. If the append itself fails (for example a short
write), the exception propagates before memory is touched, so a failed `write`
has no effect.

`remove` is logged even when the key does not exist; replay treats it as a
no-op.

## Snapshot and WAL truncation

Triggered by `Options` thresholds (write count, written bytes, interval, via
`SnapshotScheduler`) and unconditionally on clean shutdown. Runs under
`write_mutex_`, so no write can land between the snapshot and the truncation:

1. `lsn = wal.current_lsn()`.
2. Serialize the whole store, stamped with `lsn`, into memory.
3. `write_file_bytes(P.tmp)`, fsynced in sync mode.
4. `replace_file(P.tmp, P)`: rename over the old snapshot; in sync mode the
   directory is fsynced (POSIX) or `MOVEFILE_WRITE_THROUGH` is used (Windows).
5. `wal.compact(lsn)`: copy every valid record with `LSN > lsn` into
   `P.wal.tmp`, then `replace_file(P.wal.tmp, P.wal)`. Under the write mutex
   this normally leaves an empty WAL. Because `lsn` is `current_lsn()` taken
   while writers are excluded, it never falls inside a batch, so compaction
   keeps or drops whole batches.

Every crash point leaves a recoverable state:

| Crash                         | On-disk state                         | Recovery result                          |
|-------------------------------|---------------------------------------|------------------------------------------|
| during step 3                 | old `P`, full WAL, partial `P.tmp`    | old snapshot + WAL replay                |
| during step 4 (rename)        | either old or new `P` (atomic)        | old: as above; new: WAL records skipped by LSN |
| after step 4, before step 5   | new `P`, full WAL                     | records with `LSN <= snapshot LSN` skipped |
| during step 5                 | new `P`, old or compacted WAL         | same as above                            |

The "skip by LSN" rule is what makes step 5 safely re-doable and the whole
procedure idempotent.

## Recovery algorithm

Run by the `Database` constructor (`load_from_disk`):

1. If `P` exists, load it into `Storage`; `snapshot_lsn = last_lsn` from the
   header. Otherwise start empty with `snapshot_lsn = 0`.
2. `wal.advance_lsn_to(snapshot_lsn)` so new LSNs are strictly greater.
3. Replay `P.wal` front to back. `Put`/`Remove` records outside a batch are
   applied immediately; records between `Begin` and `Commit` are buffered and
   applied, in order, when the `Commit` is read. For each operation:
   - `LSN <= snapshot_lsn`: skip (already in the snapshot);
   - otherwise apply it to `Storage`.
4. Stop at the first record that fails validation (torn tail from a crash
   mid-append, or corruption) and ignore it and everything after it, logging a
   warning to stderr. Later bytes cannot be trusted because the record
   boundaries are unknown. The same applies to misplaced framing (a `Begin`
   inside a batch, or a `Commit` with no `Begin`). If the log ends while a batch
   is still open, its buffered operations are discarded.
5. Set the next LSN to `max(snapshot_lsn, highest LSN seen) + 1`. LSNs of
   discarded records count, so they are never reused.
6. If any operation was applied, or the WAL ended uncleanly (corrupt tail,
   bad framing or an uncommitted batch), take a snapshot and truncate the WAL
   immediately. This removes the bad bytes; otherwise new appends would land
   after them and be unreachable (or, after an open `Begin`, be absorbed into
   the dead batch) on the next replay.

Guarantee: every write or batch acknowledged in sync mode before the crash is
recovered, no partially written record is ever applied, and a batch is
recovered either entirely or not at all. In async mode, acknowledged writes
that were still in OS buffers can be lost, but recovery still yields a
consistent prefix of the operation history.

## Invariants

- Records in `P.wal` have strictly increasing LSNs.
- A new record's LSN is greater than the snapshot LSN and every LSN already
  in the WAL.
- The in-memory state equals `snapshot + replay(committed WAL operations with LSN > snapshot LSN)`.
- Every `Begin` in a well-formed WAL is followed by its `Commit` before any
  other `Begin`, and only `Put`/`Remove` records lie between them.
- A snapshot replaces `P` atomically; `P` is never partially written.

## Alternatives rejected

- **Physical/page logging.** Not applicable while the whole dataset is an
  in-memory map; revisited in M4.1.
- **Per-record CRC over the length prefix too.** Not needed: a wrong length is
  already caught by the cap, EOF, or the payload CRC.
- **Rewriting the WAL in place on truncation.** A rename of a freshly written
  file is atomic; in-place truncation is not.
- **One multi-operation record per batch.** It would be atomic for free (a
  single CRC), but the `Begin`/`Commit` framing teaches the commit-record rule
  that the transaction manager (M4.2) and ARIES (M4.1) build on, and keeps
  records small and bounded by the per-record size cap.

## Known limitations

- **Snapshot has no magic, version or checksum.** Corruption of the snapshot
  file is not detected: `load_snapshot` does not check stream state, so a
  truncated or damaged snapshot can load partial data silently. Atomic rename
  protects against our own crashes, not against media corruption. Planned
  under "Format versioning" in the roadmap.
- **No format version in the WAL either**, so the format cannot be evolved
  with a refuse-to-open guard yet.
- **Native byte order** and `sizeof(LogRecordType)` are baked into the format.
- **Whole-file work:** every snapshot rewrites the entire dataset and the
  snapshot is built in memory; this goes away with M3.
- **No group commit:** each write is its own `fsync`.
- **Single writer:** writes are fully serialized by `write_mutex_`, including
  during a snapshot.
- **A failed append leaves its partial bytes in the live WAL.** If a write
  fails midway (e.g. disk full), `put`/`write` throws and memory is unchanged,
  but the torn bytes stay at the end of the file until the next compaction or
  recovery. Records appended after them in the same run would be unreachable
  on replay. This affects single records and batches alike; truncating the file
  back to its previous length on failure is the fix.
- **Compaction fsync:** records copied by `compact` are appended with
  `kAsync` and the file is synced once afterwards by `replace_file`.

## Tests

Format, torn-tail and corruption handling: `wal_tests.cpp`,
`logrecord_tests.cpp`. Crash-consistency and fault injection:
`durability_tests.cpp`. Recovery rules, including LSN continuity after a clean
shutdown and removal of a corrupt WAL tail: `recovery_tests.cpp`. Batches
(framing, every torn prefix of a batch, fault-injected write, concurrent
readers, randomized comparison against a model): `batch_tests.cpp`.
