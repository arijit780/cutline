#pragma once
#include <cstdint>
#include <cstddef>

struct Bytes {
    const uint8_t* data;
    size_t len; 
};

using log_sequence_number = uint64_t;

struct WalAppendResult {
    log_sequence_number lsn;
};
class WriteAheadLog {
public:
    virtual ~WriteAheadLog() = default;
    virtual WalAppendResult append(const Bytes& record) = 0; // append raw bytes to the log

    virtual void flush(log_sequence_number lsn) = 0; // ensure all appended records are durable

    virtual void replay(
        void (*consumer)(const Bytes& record, void* ctx),
        void* ctx
    ) = 0; // replay all records from the log, calling consumer for each record
};
