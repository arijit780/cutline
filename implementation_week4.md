# Week 4: Transactional Atomicity with Redo-Only Logging

## Overview

This document specifies single-writer, crash-safe transactions using redo-only WAL (Write-Ahead Log) without undo logging, MVCC, or concurrency control. Recovery applies only committed transactions from the WAL prefix.

---

## Design Constraints

- **Single writer** — No write-write concurrency
- **Redo-only** — No undo or rollback during recovery
- **Prefix-based recovery** — First detected corruption halts replay
- **Atomicity guarantee** — Transaction is durable iff COMMIT record exists in valid WAL prefix
- **No snapshotting** — All state derived from MemTable + WAL replay

---

## Core Invariant

**A transaction is durable if and only if its TX_COMMIT record has been fsync'd to the WAL.**

Implications:
- BEGIN is a logical marker; has no durability meaning
- PUT/DELETE operations have no durability meaning
- COMMIT + fsync is the sole atomicity boundary
- Crash before COMMIT ⟹ transaction is discarded
- Crash after COMMIT+fsync ⟹ transaction must be applied

---

## Transaction Lifecycle

### Write Phase
1. Application calls `begin_tx(txid)`
2. For each update: call `tx_put(txid, key, value)` or `tx_delete(txid, key)`
3. Each call appends to WAL but does NOT modify MemTable
4. Updates buffered locally (in application, not WAL-level)

### Commit Phase
1. Application calls `commit_tx(txid)`
2. WAL appends COMMIT record
3. WAL calls fsync() for durability
4. **Only after fsync succeeds**, atomically apply buffered updates to MemTable

### Crash Scenarios
| Crash Point | Result |
|-------------|--------|
| During write phase | Transaction discarded on replay |
| Before COMMIT+fsync | Transaction discarded on replay |
| After COMMIT+fsync | Transaction applied on replay |

---

## Recovery (Replay) Algorithm

### Scan Phase
Read WAL sequentially:

1. **Validate header** — magic, version
2. **Validate sizes** — key_len ≤ MAX_KEY_SIZE, value_len ≤ MAX_VALUE_SIZE
3. **Read payload and CRC** — detect torn writes
4. **Verify CRC** — confirm record integrity
5. **Stop on first failure** — never resynchronize or skip ahead

### Collect Phase
For each valid record:
- **BEGIN**: Initialize `pending[txid] = []`
- **PUT/DELETE**: Append record to `pending[txid]`
- **COMMIT**: Record `txid` in `committed` set

### Apply Phase
For each `txid` in `committed` (in WAL order):
- Apply all records in `pending[txid]` to MemTable atomically

Uncommitted transactions are discarded.

---

## Record Format

All records include CRC over header + payload:

| Record Type | Fields | Payload |
|-------------|--------|---------|
| BEGIN | txid, magic, version | (empty) |
| PUT | txid, key_len, value_len | key \| value |
| DELETE | txid, key_len | key |
| COMMIT | txid, magic, version | (empty) |

CRC is appended after payload; computed before writing.

---

## Durability Requirements

### Per-Operation
- `append()` does NOT fsync (batched)
- `tx_put()`, `tx_delete()` do NOT fsync

### Per-Transaction
- `commit_tx()` must call fsync() after writing COMMIT record
- No fsync before COMMIT is valid

### Rationale
Fsync per operation destroys batching and misunderstands durability boundaries. Only the COMMIT operation is a durability boundary.

---

## Runtime Isolation (No Partial Visibility)

To prevent readers from seeing uncommitted writes:

```cpp
begin_tx(txid):
    tx_buffer[txid] = {}

tx_put(txid, key, value):
    wal.tx_put(...)           // Write to WAL
    tx_buffer[txid][key] = value  // Buffer in memory

commit_tx(txid):
    wal.commit_tx(txid)       // Write COMMIT + fsync
    // Only after fsync succeeds:
    apply tx_buffer[txid] atomically to MemTable
    delete tx_buffer[txid]
```

**Readers read only MemTable** — they never see buffered (uncommitted) writes.

---

## Append-Time Validation

The WAL append path must reject malformed records:

- **Nested BEGIN**: Reject if txid already in pending
- **PUT/DELETE without BEGIN**: Reject if txid not in pending
- **COMMIT without BEGIN**: Reject if txid not in pending
- **Duplicate COMMIT**: Reject if txid already committed

These checks prevent corruption from application errors and must happen before fsync.

---

## Payload Size Limits

Always validate before allocation:

```cpp
if (hdr.key_len > WAL_MAX_KEY_SIZE || hdr.value_len > WAL_MAX_VALUE_SIZE) {
    // Corruption detected: stop replay
}
```

Corrupted headers could request unbounded memory. Limits prevent OOM during recovery.

---

## Multi-Key Atomicity

Example transaction:
```
BEGIN txid=1
PUT A=1
PUT B=2
DEL C
PUT A=3
COMMIT txid=1
```

Final state (on successful commit):
- A → 3 (overwritten)
- B → 2
- C → removed

Atomicity is guaranteed: either all updates appear or none do. The number of keys is irrelevant because buffering is per-transaction, not per-key.

---

## Commit Ordering Invariant

Transactions must be applied to MemTable in **WAL commit order**, not hash iteration order.

**Correct approach**:
```cpp
vector<uint64_t> commit_order;
// ... during scan: commit_order.push_back(txid) when COMMIT seen
// ... during apply: for (auto txid : commit_order) apply(txid)
```

This preserves causal ordering if transactions have overlapping keys.

---

## Known Limitations

These design choices deliberately omit optimizations for correctness:

1. **Full buffering in memory** — Large WALs spike memory. Future: streaming apply-on-commit.
2. **No WAL truncation** — WAL grows unbounded. Future: checkpointing.
3. **No group commit** — Each TX_COMMIT fsync is independent. Future: batch commits.
4. **No replication** — Single machine. Future: log shipping.
5. **No MVCC** — Readers wait for single writer. Future: multi-version snapshots.

---

## Verification Checklist

Before accepting this design:

- [ ] Can a transaction partially apply after crash? → **No**
- [ ] Can WAL replay alone determine correctness? → **Yes**
- [ ] Is COMMIT the only durability boundary? → **Yes**
- [ ] Does aborting a transaction require recovery work? → **No**
- [ ] Is atomicity independent of key count? → **Yes**
- [ ] Does CRC prevent corruption from being applied? → **Yes**

---

## Implementation Summary

| Component | Responsibility |
|-----------|-----------------|
| `begin_tx(txid)` | Write BEGIN record, create buffer |
| `tx_put(txid, key, value)` | Validate txid, write PUT, buffer locally |
| `tx_delete(txid, key)` | Validate txid, write DELETE, buffer locally |
| `commit_tx(txid)` | Write COMMIT, fsync, apply buffer to MemTable |
| `replay(apply)` | Scan, collect, apply committed TXs only |

All records are self-describing; recovery needs only magic, version, CRC, and record type to determine correctness.

---

