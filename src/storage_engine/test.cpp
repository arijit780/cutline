#include "in_memory_kv.h"
#include <iostream>

int main() {
    InMemoryKV kv;
    // Test reading a non-existent key
    Bytes key1 = {(uint8_t*)"key1", 4};      // Create key "key1"
    Bytes value1 = {nullptr, 0};             // Create empty value holder
    // {} for the options uses default constructed ReadOptions
    Status s = kv.read(key1, {}, &value1);   // Try to read the key
    // Check if we got NOT_FOUND status (expected, since we never wrote it)
    std::cout << "Read non-existent key: " << (s.code == Status::Code::NOT_FOUND ? "PASS" : "FAIL") << std::endl;
    
    Bytes value_to_write = {(uint8_t*)"hello", 5};  // Create value "hello"
    kv.apply_mutation(key1, value_to_write, {0});   // Write it to the store
    Bytes read_value = {nullptr, 0};                // Create empty value holder
    s = kv.read(key1, {}, &read_value);             // Read it back
    // Check if we got OK status (expected, since we just wrote it)
    std::cout << "Write and read back: " << (s.code == Status::Code::OK ? "PASS" : "FAIL") << std::endl;
    std::cout << "Read value: " << std::string(reinterpret_cast<const char*>(read_value.data), read_value.len) << std::endl;
    return 0;
}