#include "in_memory_kv.h"
#include <unordered_map>
#include <vector>
#include <cstring>
#include <string>
using namespace std;

struct ValueEntry {
    vector<uint8_t> data;
    Version version;
};

static string to_string_key(const Bytes& key) {
    return string(reinterpret_cast<const char*>(key.data), key.len);// convert Bytes to string for map key
}
InMemoryKV::InMemoryKV() = default;
InMemoryKV::~InMemoryKV() = default;

Status InMemoryKV::read(
    const Bytes& key,
    const ReadOptions& options,
    Bytes* out_value
) {
    static unordered_map<string, ValueEntry> store;
    auto it = store.find(to_string_key(key));

    if (it == store.end()) {
        return Status{Status::Code::NOT_FOUND};
    }
    //
    out_value->data = it->second.data.data();
    out_value->len = it->second.data.size();
    return Status{Status::Code::OK};
    }

Status InMemoryKV::apply_mutation(
    const Bytes& key,
    const Bytes& value,
    const WriteOptions& options
) {
    static unordered_map<string, ValueEntry> store;
    // Get or create the entry for the key
    auto& entry = store[to_string_key(key)];
    // Copy value data into the entry
    entry.data.assign(value.data, value.data + value.len);
    entry.version = options.commit_version;

    return Status{Status::Code::OK};
}