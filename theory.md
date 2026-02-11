Here’s a **clean, well-arranged, internally consistent version** of your notes. I’ve **only reorganized and tightened**, not changed meaning.

---

# KV Store Theory

## Weeks 1–2 — Storage Engine Boundaries

### Architectural constraints

* Only the **StorageAdapter** may call into the storage engine.
* The storage engine exposes **mutation and read primitives only**.
* CRUD semantics, queries, and user intent must **not** appear in storage APIs.
* The storage engine may depend **only** on filesystem and OS primitives.
* The storage engine must not know about:

  * networking
  * queries
  * clients
  * transactions
* The storage engine must be **fully swappable** without recompiling server logic.

---

### WAL as an internal mechanism

* WAL is an **internal mechanism** for durability and crash recovery.
* WAL concepts must **not leak** outside the storage module.
* No WAL symbols (LSN, offsets, flush) may appear in public APIs.
* The storage engine may use WAL internally but must not expose it.

---

### WAL responsibilities

* All mutations must be written to WAL **before** being applied.
* WAL is **append-only**, operating on raw byte records.
* WAL uses **log sequence numbers (LSN)** to define ordering and durability.
* WAL must support:

  * append
  * flush up to a specific LSN
  * replay to reconstruct state after crash
* WAL implementation details remain **opaque** to callers.

---

## Week 3 — Durability & Filesystem Reality

### Durability (the contract)

* Durability is **user-visible**, not a storage detail.
* **If `put()` returns OK, the write must survive a crash.**
* Volatile state (CPU, memory, page cache) is allowed to disappear.
* Only **crash-stable storage** counts.

---

### What exists on disk

* Persistence is **per inode**, not per path.
* A file consists of **three independent objects**:

  * **Directory inode + blocks** → filename → inode mapping
  * **File inode** → size, block pointers, metadata
  * **Data blocks** → actual bytes
* These objects are dirtied and flushed **independently**.

---

### `fsync` / `fdatasync` semantics (inode-level)

* `fsync(fd)`

  * Flushes:

    * data blocks
    * **all** inode metadata
  * Does **not** flush:

    * directory entries

* `fdatasync(fd)`

  * Flushes:

    * data blocks
    * **minimal inode metadata** (size, block pointers)
  * Skips:

    * timestamps
    * permissions
    * ownership
  * Does **not** flush:

    * directory entries

* `fsync(dirfd)`

  * Flushes:

    * directory data blocks
    * directory inode
  * Guarantees:

    * filename → inode mapping is durable

---

### Core invariant

* **fsync applies to exactly one inode.**
* There is **no implicit propagation** to:

  * parent directories
  * child files
  * related inodes

---

### New file / namespace changes

* Creating, renaming, or unlinking a file dirties:

  * directory inode
  * file inode
  * data blocks
* Required for durability:

  * `fdatasync(fd)` or `fsync(fd)` → file contents + inode
  * `fsync(dirfd)` → name → inode mapping
* `fsync(fd)` alone is **insufficient**.
* `fdatasync(fd)` alone is **insufficient**.

---

### Existing file (steady state)

* File already exists.
* Directory entry already durable.
* No renames or deletes.
* Only dirty:

  * data blocks
  * possibly file size / block pointers.
* **Minimal correct operation**:

  * `fdatasync(fd)`
* No directory fsync needed.
* Dropping sync entirely **breaks durability**.

---

### Why databases preallocate WAL

* Directory fsyncs are expensive.
* WAL design eliminates directory changes from the hot path:

  * create WAL files once
  * `fsync(dirfd)` once
  * preallocate file size
  * steady state uses `fdatasync(fd)`
* This is a **correctness requirement**, not an optimization.

---

## Final mental model post week 3

* Durability = **reachable inode + correct data blocks**
* `fdatasync` is the **cheapest correct primitive**
* It works **only** when directory durability is already solved
* Everything else is an implementation detail


