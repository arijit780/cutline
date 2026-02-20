# Storage Engine Theory

## Architecture Constraints

- Only the **StorageAdapter** may call into the storage engine
- Storage engine exposes **mutation and read primitives only**
- CRUD semantics, queries, and user intent must **not** appear in storage APIs
- Storage engine depends **only** on filesystem and OS primitives
- Storage engine must not know about: networking, queries, clients, transactions
- Storage engine must be **fully swappable** without recompiling server logic

---

## WAL as Internal Mechanism

- WAL is an **internal mechanism** for durability and crash recovery
- WAL concepts must **not leak** outside the storage module
- No WAL symbols (LSN, offsets, flush) may appear in public APIs
- Storage engine may use WAL internally but must not expose it
- All mutations must be written to WAL **before** being applied
- WAL is **append-only**, operating on raw byte records
- WAL must support: append, flush up to LSN, replay for recovery

---

## Durability Contract

- **Durability is user-visible, not a storage detail**
- If `put()` returns OK, the write must survive a crash
- Volatile state (CPU, memory, page cache) is allowed to disappear
- Only **crash-stable storage** counts

---

## Filesystem Reality: fsync Semantics

### What exists on disk

Persistence is **per inode**, not per path. A file consists of:
- **Directory inode + blocks** → filename → inode mapping
- **File inode** → size, block pointers, metadata
- **Data blocks** → actual bytes

These objects are dirtied and flushed **independently**.

### fsync(fd)
- Flushes: data blocks, **all** inode metadata
- Does NOT flush: directory entries

### fdatasync(fd)
- Flushes: data blocks, **minimal** inode metadata (size, block pointers)
- Skips: timestamps, permissions, ownership
- Does NOT flush: directory entries

### fsync(dirfd)
- Flushes: directory data blocks, directory inode
- Guarantees: filename → inode mapping is durable

---

## Core Invariant: fsync applies to exactly one inode

There is **no implicit propagation** to:
- parent directories
- child files
- related inodes

---

## New File / Namespace Changes

Creating, renaming, or unlinking a file dirties:
- directory inode
- file inode
- data blocks

Required for durability:
- `fdatasync(fd)` or `fsync(fd)` → file contents + inode
- `fsync(dirfd)` → name → inode mapping

Both are necessary. Neither alone is sufficient.

---

## Existing File (Steady State)

When file already exists and directory entry is already durable (no renames/deletes):
- Only dirty: data blocks, possibly file size / block pointers
- **Minimal correct operation**: `fdatasync(fd)`
- No directory fsync needed
- Dropping sync entirely **breaks durability**

---

## Why Databases Preallocate WAL

Directory fsyncs are expensive. WAL design eliminates directory changes from the hot path:
- Create WAL files once
- `fsync(dirfd)` once
- Preallocate file size
- Steady state uses `fdatasync(fd)` only

This is a **correctness requirement**, not an optimization.

---

## Final Mental Model

- **Durability** = reachable inode + correct data blocks
- **fdatasync** is the cheapest correct primitive
- It works **only** when directory durability is already solved
- Everything else is implementation detail
