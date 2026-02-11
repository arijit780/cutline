
All multi-byte fields are little-endian.

---

| magic (4B) |
| version (1B) |
| type (1B) | // PUT / DELETE
| key_len (4B) |
| val_len (4B) |
| key bytes |
| value bytes |
| crc32 (4B) |
## Field-by-Field Justification

### `magic` (4 bytes)

Purpose:
- Marks the start of a record
- Enables resynchronization after corruption
- Distinguishes records from random bytes

Properties:
- Fixed constant
- Unlikely to appear accidentally
- Checked before any parsing

Without magic:
- Recovery cannot safely resync
- Garbage may be misinterpreted as headers

---

### `version` (1 byte)

Purpose:
- Defines how the rest of the record is interpreted
- Enables format evolution

Properties:
- Interpreted per-record
- Allows mixed-version WALs during upgrade

Whole-file versioning is insufficient for crash recovery.

---

### `type` (1 byte)

Purpose:
- Defines replay semantics
- Avoids inference from payload shape

Examples:
- PUT
- DELETE

Inference from lengths is ambiguous and unsafe.

---

### `key_len` / `val_len` (4 bytes each)

Purpose:
- Self-delimiting records
- Bounds checking before allocation
- Forward skipping without decoding payloads

Properties:
- Fixed-width for deterministic stepping
- Trusted only after sanity checks

Without explicit lengths:
- Replay becomes fragile and stateful

---

### `key bytes` / `value bytes`

Purpose:
- Opaque payload
- WAL does not interpret contents

The WAL transports bytes only.
All semantic meaning lives above the WAL layer.

---

### `crc32` (4 bytes, at end)

Purpose:
- Detects partial writes
- Detects torn records
- Detects silent corruption

Why at the end:
- Validates the entire record
- Allows detection after full read
- Cleanly distinguishes complete vs incomplete records

CRC is for **integrity**, not alignment.

---

## Replay Model

Replay operates under these assumptions:

- Replay may start at any byte offset
- The WAL tail may be garbage
- Length fields are untrusted until validated
- Only fully validated records may be applied

### Replay Loop (Conceptual)

1. Scan for `magic`
2. Read fixed-size header
3. Validate version and lengths
4. Read payload
5. Validate CRC
6. Apply record or skip
7. Continue forward

Replay never guesses.
Invalid records are rejected deterministically.

---

## Corruption Handling

Possible corruption scenarios:

- Partial header
- Partial payload
- Missing CRC
- Random bytes at tail
- Mid-file corruption

Correct behavior:

- Never apply a record with invalid CRC
- Never trust unvalidated lengths
- Stop or resynchronize safely
- Never crash during recovery

---

## What the WAL Does *Not* Guarantee

- Atomic multi-record transactions
- Logical consistency across records
- Semantic validation of keys or values

The WAL guarantees **durable ordering**, not correctness.

---

## Design Principles (Non-Negotiable)

- No WAL concepts leak outside the storage engine
- WAL format is a stable on-disk contract
- WAL replay must be deterministic
- Corruption must be detectable, not guessed around

---

## Summary

- The WAL is a crash-tolerant log, not a serializer
- Records are self-describing and independently verifiable
- Magic enables resynchronization
- Lengths enable skipping
- CRC enables integrity checking
- Garbage tail is expected and safe

If any of these assumptions are violated, the WAL design is incorrect.
