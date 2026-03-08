#pragma once

#include "storage_engine.h"
#include <atomic>
#include <vector>
#include <map>

// Week 5 (revised): Copy‑on‑write MemTable
//
// Instead of an append‑only list of updates, maintain a fully materialized
// map of the latest state. Readers snapshot an atomic pointer to the current
// "root" map. Writers clone the map, apply the mutation, then publish the
// new pointer with release semantics. Old maps are leaked (reclaimed later
// in Week 7).

struct ValueEntry {
    std::vector<uint8_t> value;
    Version version;
};

using MemTable = std::map<std::vector<uint8_t>, ValueEntry>;

class InMemoryKV : public StorageEngine {
    public:
        InMemoryKV();
        ~InMemoryKV() override;

        Status read(
            const Bytes& key,
            const ReadOptions& options,
            Bytes* out_value
        ) override;

        Status apply_mutation(
            const Bytes& key,
            const Bytes& value,
            const WriteOptions& options
        ) override;

    private:
        // Atomic pointer to current memtable (copy‑on‑write root)
        // Invariant: map object is immutable after publication
        // Memory is leaked (intentional for Week 5; reclamation in Week 7)
        std::atomic<MemTable*> root_;
};
