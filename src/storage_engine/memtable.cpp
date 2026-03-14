#include "memtable.h"
#include <cstring>

InMemoryKV::InMemoryKV() {
    // Invariant: root_ always points to a valid MemTable
    root_.store(new MemTable(), std::memory_order_release);
}

InMemoryKV::~InMemoryKV() {
    // Week 5: Memory leak is intentional.
    // Old MemTable versions remain reachable by readers.
    // Reclamation will be implemented later (epoch GC / hazard pointers).
}

Status InMemoryKV::read(
    const Bytes& key,
    const ReadOptions& options,
    Bytes* out_value
) {
    // Acquire snapshot of current memtable
    MemTable* table = root_.load(std::memory_order_acquire);

    // Convert lookup key
    std::vector<uint8_t> key_vec(key.data, key.data + key.len);

    auto it = table->find(key_vec);
    if (it == table->end()) {
        return Status{Status::Code::NOT_FOUND};
    }

    const ValueEntry& entry = it->second;

    // Version visibility check
    if (entry.version > options.visible_up_to) {
        return Status{Status::Code::NOT_FOUND};
    }

    out_value->data = entry.value.data();
    out_value->len  = entry.value.size();

    return Status{Status::Code::OK};
}

Status InMemoryKV::apply_mutation(
    const Bytes& key,
    const Bytes& value,
    const WriteOptions& options
) {
    // Convert inputs to owning vectors
    std::vector<uint8_t> key_vec(key.data, key.data + key.len);
    std::vector<uint8_t> value_vec(value.data, value.data + value.len);

    // Snapshot current root
    MemTable* old_table = root_.load(std::memory_order_acquire);

    // Copy-on-write clone
    MemTable* new_table = new MemTable(*old_table);

    // Apply mutation
    (*new_table)[std::move(key_vec)] =
        ValueEntry{std::move(value_vec), options.commit_version};

    // Publish new root
    root_.store(new_table, std::memory_order_release);

    // Old tables intentionally leaked for now
    return Status{Status::Code::OK};
}

const MemTable* InMemoryKV::snapshot() const {
    // Readers and snapshot subsystem obtain immutable root
    return root_.load(std::memory_order_acquire);
}