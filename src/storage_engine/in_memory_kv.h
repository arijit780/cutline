#pragma once

#include "storage_engine.h"
#include <atomic>
#include <vector>

// Week 5: Lock-Free, Append-Only Linked List
struct Node {
    std::vector<uint8_t> key;
    std::vector<uint8_t> value;
    uint64_t version;
    Node* next;
    
    Node(const std::vector<uint8_t>& k, const std::vector<uint8_t>& v, uint64_t ver, Node* n)
        : key(k), value(v), version(ver), next(n) {}
};

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
        // Global head pointer: atomic publication point
        // Invariant: next pointers immutable after publication
        // Memory is leaked (intentional for Week 5)
        std::atomic<Node*> head_;
};
