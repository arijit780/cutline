#include "in_memory_kv.h"
#include <cstring>

InMemoryKV::InMemoryKV() : head_(nullptr) {}

InMemoryKV::~InMemoryKV() {
    // Week 5: Memory leak is intentional.
    // Readers may still traverse old versions.
    // Reclamation deferred to Week 7 (RCU/hazard pointers).
}

Status InMemoryKV::read(
    const Bytes& key,
    const ReadOptions& options,
    Bytes* out_value
) {
    // Snapshot current head with acquire semantics
    // This ensures all writes before the writer's store(release) are visible
    Node* snapshot = head_.load(std::memory_order_acquire);
    
    // Traverse snapshot without holding lock
    // Snapshot is immutable; readers are concurrent
    for (Node* curr = snapshot; curr != nullptr; curr = curr->next) {
        // Check version visibility
        if (curr->version > options.visible_up_to) {
            continue; // Skip future versions
        }
        
        // Check key match
        if (curr->key.size() == key.len && 
            std::memcmp(curr->key.data(), key.data, key.len) == 0) {
            // Found matching key with visible version
            out_value->data = curr->value.data();
            out_value->len = curr->value.size();
            return Status{Status::Code::OK};
        }
    }
    
    return Status{Status::Code::NOT_FOUND};
}

Status InMemoryKV::apply_mutation(
    const Bytes& key,
    const Bytes& value,
    const WriteOptions& options
) {
    // Convert key to vector
    std::vector<uint8_t> key_vec(key.data, key.data + key.len);
    std::vector<uint8_t> value_vec(value.data, value.data + value.len);
    
    // Snapshot current head (off-list construction)
    Node* old_head = head_.load(std::memory_order_acquire);
    
    // Create new node with old_head as next
    // This node is not yet visible to readers
    Node* new_node = new Node(key_vec, value_vec, options.commit_version, old_head);
    
    // Atomic publication with release semantics
    // All mutations to new_node are now visible before any reader observes it
    head_.store(new_node, std::memory_order_release);
    
    return Status{Status::Code::OK};
}
