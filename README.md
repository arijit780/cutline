# KV Store — Single-Writer, Crash-Safe Storage Engine

## What This Is

A minimal, correct key-value storage engine with:
- **Durability**: Writes persisted via Write-Ahead Log (WAL) + fsync
- **Atomicity**: Multi-key transactions with redo-only recovery
- **Single writer**: No concurrency control
- **Crash safety**: WAL replay reconstructs committed state

## Core Invariants

1. **A transaction is durable iff its COMMIT record is fsync'd**
   - BEGIN has no durability meaning
   - PUT/DELETE have no durability meaning
   - Only COMMIT + fsync guarantees atomicity

2. **Recovery applies only committed transactions**
   - Scan WAL sequentially
   - Collect records by transaction
   - Apply only committed transactions
   - First corruption halts replay

3. **Runtime isolation without MVCC**
   - Readers read only MemTable
   - Writers buffer uncommitted changes
   - Changes applied only after COMMIT+fsync

## WAL Record Format

```
[Header: 22 bytes]
  magic      (4B)   = 0x57414C32 ("WAL2")
  version    (1B)   = 0x02
  type       (1B)   = BEGIN|PUT|DELETE|COMMIT
  txid       (8B)   = transaction ID
  key_len    (4B)   = key size in bytes
  value_len  (4B)   = value size in bytes

[Payload]
  key        (key_len bytes)
  value      (value_len bytes)

[Checksum]
  crc32      (4B)   = CRC over header + payload
```

## API

```cpp
void begin_tx(uint64_t txid);
void tx_put(uint64_t txid, const vector<uint8_t>& key, const vector<uint8_t>& value);
void tx_delete(uint64_t txid, const vector<uint8_t>& key);
void commit_tx(uint64_t txid);    // fsync included
void replay(function<void(const LogRecord&)> apply);
```

## Crash Semantics

| Crash Point | Outcome |
|-------------|---------|
| During write | Transaction discarded |
| Before COMMIT+fsync | Transaction discarded |
| After COMMIT+fsync | Transaction applied |

## Files

- `wal.h/cpp` — WAL implementation (append, commit, replay)
- `wal_format.h` — Record format, constants
- `storage_engine.h` — Interface
- `in_memory_kv.h/cpp` — MemTable (not integrated)

## Design

- **Redo-only**: No undo; replay applies only committed TXs
- **Prefix-based**: First corruption halts replay
- **Fsync at COMMIT**: COMMIT is durability boundary
- **CRC validation**: Detects corruption
- **Size limits**: Prevents OOM
