// WAL binary format v1.   here we define the on-disk contract and no logic here.

#pragma once
#include <stdint.h>

// ---- WAL invariants ----
// - Records are append-only
// - Records are self-delimiting
// - Records are checksummed
// - Replay may start at any byte offset
// - Corruption must be detectable and skippable

// 4 byte magic for resynchronization
#define WAL_MAGIC 0x57414C31 // 32 bits

// WAL record version
#define WAL_VERSION_V1 0x01 // wal may contain mixed versions after upgrades, so we need a version field in the header to distinguish them. This allows us to evolve the format in the future while maintaining backward compatibility with old records.
// 8 bit type field to allow multiple record types in the future (e.g. put, delete, etc.)
// WAL record types
enum wal_record_type : uint8_t{
    WAL_RECORD_PUT = 0X01,
    WAL_RECORD_DELETE = 0X02,
};


// fixed size wal record header for version 1
struct wal_record_header_v1 {
    uint32_t magic; // WAL_MAGIC
    uint8_t version; // WAL_VERSION_V1
    uint8_t type; // wal_record_type
    uint32_t key_len; // length of key in bytes
    uint32_t value_len; // length of value in bytes
};
// Record layout:

// | wal_record_header_v1 |
// | key bytes            |
// | value bytes          |
// | crc32 (uint32_t)     |

// crc32 is computed over:
//   header (excluding crc field) + key bytes + value bytes

// the checksum is stored at the end to detect torn writes
// and allow validation only after the full record is read
// Recovery loop:
//     Read 4 bytes
//     Compare with WAL_MAGIC
//     If mismatch:
//         advance by 1 byte
//         repeat
//     If match:
//         attempt to parse header
//         validate lengths
//         read payload
//         validate CRC
//         apply or reject