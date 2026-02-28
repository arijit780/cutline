# Week 5: Concurrent Readers via Lock-Free Publication

## Objective

Enable concurrent readers while maintaining single-writer durability. No MVCC yet—just lock-free snapshots within the existing `StorageEngine` abstraction.

---

## Design Adaptation

Replaced the static `unordered_map<string, ValueEntry>` in `InMemoryKV` with an append-only linked list:

```cpp
// Week 5: Lock-Free, Append-Only Linked List
struct Node {
    vector<uint8_t> key;
    vector<uint8_t> value;
    uint64_t version;
    Node* next;
};

class InMemoryKV : public StorageEngine {
private:
    atomic<Node*> head_;  // Global head: atomic publication point
};
```

**Invariant**: `next` pointers are **immutable after publication**. No node modification, no deletion (Week 5).

---

## Semantics

### Writer (apply_mutation)

```cpp
// 1. Build new node off-list (not visible yet)
vector<uint8_t> key_vec(key.data, key.data + key.len);
vector<uint8_t> value_vec(value.data, value.data + value.len);
Node* old_head = head_.load(acquire);
Node* new_node = new Node(key_vec, value_vec, options.commit_version, old_head);

// 2. Atomic publication with release semantics
// All mutations to new_node now visible before any reader observes it
head_.store(new_node, release);
```

**Key property**: `store(release)` ensures all mutations to `new_node` are visible before any reader observes it via `load(acquire)`.

### Reader (read)

```cpp
// 1. Snapshot current head with acquire semantics
// load(acquire) sees all writes the writer performed before store(release)
Node* snapshot = head_.load(acquire);

// 2. Traverse immutable snapshot without lock
for (Node* curr = snapshot; curr != nullptr; curr = curr->next) {
    if (curr->version <= options.visible_up_to && key_matches(curr, key)) {
        out_value->data = curr->value.data();
        out_value->len = curr->value.size();
        return Status{Status::Code::OK};
    }
}
```

**Key property**: Reader traverses stable snapshot. No writer modifies existing nodes; readers never contend.

---

## Correctness Argument

1. **Structural Safety**: Writer never modifies existing nodes. Readers traverse immutable snapshots.
2. **Visibility**: Publication (store/release) happens after WAL fsync (by contract), so durability precedes visibility.
3. **Isolation**: Each reader gets a point-in-time snapshot (head_ value at read time). No partial visibility.
4. **Concurrency**: Readers proceed independently. No locks. Single writer proceeds without stalling.

| Phase | Writer | Reader |
|-------|--------|--------|
| WAL fsync | ✓ | — |
| head_.store(release) | ✓ | — |
| head_.load(acquire) | — | ✓ (sees all prior writes) |
| traverse(snapshot) | — | ✓ (immutable) |

---

## Integration with StorageEngine Interface

The abstract `StorageEngine` interface remains unchanged:

```cpp
virtual Status read(const Bytes& key, const ReadOptions& options, Bytes* out_value) = 0;
virtual Status apply_mutation(const Bytes& key, const Bytes& value, const WriteOptions& options) = 0;
```

`InMemoryKV` implements both with lock-free semantics internally.

**From caller's perspective**: 
- Call `apply_mutation` after `commit_tx` (WAL fsync guaranteed)
- Call `read` from any thread (safe for concurrent readers)

---

## Memory Management (Week 5)

**Leak memory intentionally.**

- No deletion of nodes until Week 7 (RCU or hazard pointers).
- Readers hold no reference counts; they just snapshot the head pointer.
- Writers never free nodes.

This is acceptable for Week 5 because:
- Correctness proof is simplified (no use-after-free possible)
- Avoiding memory reclamation bugs
- Focusing on lock-free publication semantics

---

## Key Properties

### ✓ Append-Only
- Only `head_` atomic variable changes
- Nodes are never modified after publication
- Old snapshots remain valid indefinitely

### ✓ Copy-Free
- No memcpy of entire dataset
- New node allocated once per write (O(1))
- Readers traverse pointers, no copying

### ✓ Lock-Free
- No mutexes, condition variables, or spinlocks
- Readers proceed independently without blocking
- Writer proceeds without stalling for readers

### ✓ Structurally Safe
- No use-after-free (nodes never freed in Week 5)
- No races on node fields (immutable after publish)
- Allocation happens off-list before publication

### ✓ Durable-Before-Visible
- WAL fsync precedes `apply_mutation` call (by contract)
- `apply_mutation` publishes atomically
- Crash cannot leave published state without durability

---

## Version Visibility

Existing `ReadOptions` and `WriteOptions` support version-based visibility:

```cpp
struct ReadOptions {
    Version visible_up_to;   // Only see versions <= this
};

struct WriteOptions {
    Version commit_version;  // Version of this write
};
```

Readers skip newer versions:
```cpp
if (curr->version > options.visible_up_to) {
    continue;  // Skip future versions
}
```

This enables layering of MVCC later (Week 6+) without changing this design.

---

## Limitations (Intentional for Week 5)

1. **Memory leak** — No reclamation (deferred to Week 7).
2. **No MVCC** — Readers see current head only, not versioned snapshots (single point-in-time).
3. **Single writer** — No write concurrency.
4. **Traversal latency** — Readers may traverse long snapshots on large datasets (O(n) per read).

---

## Files Modified

- `storage_engine.h` — Unchanged interface
- `in_memory_kv.h` — Added `Node` struct, `atomic<Node*> head_`
- `in_memory_kv.cpp` — Rewrote `read()` and `apply_mutation()` with lock-free logic

---

## Next Steps (Week 6+)

- **Week 6**: Hazard pointers or RCU for safe reclamation.
- **Week 7**: MVCC with versioned snapshots (multiple consistent views).
- **Week 8**: Batched writes and group commit.

---

## Testing

Single-writer + concurrent readers:
```
Writer: apply_mutation(k1, v1, version=1)
        apply_mutation(k2, v2, version=1)
        ...

Reader 1: read(k1, visible_up_to=1) → v1 (from snapshot at read time)
Reader 2: read(k2, visible_up_to=1) → v2 (from snapshot at read time)
```

Both readers see consistent (version 1) data without locks.

