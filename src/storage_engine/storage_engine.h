#pragma once
#include <cstdint>
#include <vector>

struct Bytes{
    const uint8_t* data;
    size_t len; // i need this coz i dont know i dont know what i am pointing to....data is just pointing ot the start of the bytes
};

struct Status{
    enum Code {
        OK,
        NOT_FOUND,
        ERROR
    } code;
    // Possibilities: key not found,value exists but length is zero,read failed,bug,uninitialized memory
    // We can use code to distinguish these cases
};

using Version = uint64_t;

struct ReadOptions {
    Version visible_up_to;
};

struct WriteOptions {
    Version commit_version;
};

class StorageEngine {
    public:
        virtual ~StorageEngine() = default;// to delete via base pointer

        virtual Status read(
            const Bytes& key,
            const ReadOptions& options,
            Bytes* out_value
            //out_value->data = <address storage owns>; out_value->len  = <length>;
        ) = 0;
        // = 0 means this function has no implementation here, and must be provided by someone else.
        virtual Status apply_mutation(
            const Bytes& key,
            const Bytes& value,
            const WriteOptions& options
        ) = 0;
};