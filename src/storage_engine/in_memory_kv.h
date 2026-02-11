#pragma once

#include "storage_engine.h"

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
};