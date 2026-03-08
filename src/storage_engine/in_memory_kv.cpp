#include "in_memory_kv.h"
#include <cstring>

InMemoryKV::InMemoryKV() : root_(nullptr) {}

InMemoryKV::~InMemoryKV() {
    // Week 5: Memory leak is intentional.
    // Old MemTable pointers are never freed; readers may still access them.
    // Reclamation will be handled in Week 7 (hazard pointers/RCU).
}

Status InMemoryKV::read(
    const Bytes& key,
    const ReadOptions& options,
    Bytes* out_value
) {
    // Snapshot current memtable pointer with acquire semantics
    MemTable* table = root_.load(std::memory_order_acquire);
    if (table == nullptr) {
        return Status{Status::Code::NOT_FOUND};
    }

    // Build a temporary key vector for lookup
    std::vector<uint8_t> key_vec(key.data, key.data + key.len);
    auto it = table->find(key_vec);
    if (it == table->end()) {
        return Status{Status::Code::NOT_FOUND};
    }

    const ValueEntry& entry = it->second;
    if (entry.version > options.visible_up_to) {
        return Status{Status::Code::NOT_FOUND};
    }

    out_value->data = entry.value.data();
    out_value->len = entry.value.size();
    return Status{Status::Code::OK};
}

Status InMemoryKV::apply_mutation(
    const Bytes& key,
    const Bytes& value,
    const WriteOptions& options
) {
    // Convert to owning vectors
    std::vector<uint8_t> key_vec(key.data, key.data + key.len);
    std::vector<uint8_t> value_vec(value.data, value.data + value.len);

    // Snapshot current table
    MemTable* old_table = root_.load(std::memory_order_acquire);
    // Clone map. If old_table is null, start with empty map.
    MemTable* new_table = new MemTable(old_table ? *old_table : MemTable{});

    // Apply mutation: overwrite or insert the entry
    (*new_table)[std::move(key_vec)] = ValueEntry{std::move(value_vec), options.commit_version};

    // Publish new table pointer
    root_.store(new_table, std::memory_order_release);

    // old_table is intentionally leaked; reclamation deferred to later weeks
    return Status{Status::Code::OK};
}
