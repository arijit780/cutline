// WAL binary format v2 (transactional)
// This defines the on-disk contract only. No logic here.

#pragma once
#include <stdint.h>

// ------------------------------------------------------------
// WAL Invariants
// ------------------------------------------------------------
// - Records are strictly append-only
// - Records are self-delimiting
// - Records are checksummed
// - Replay is sequential and prefix-based
// - First corrupted record terminates replay
// - No record after corruption is trusted
// - A transaction exists iff its COMMIT record exists
// - BEGIN has zero durability meaning

// ------------------------------------------------------------
// 4-byte magic for record boundary detection
// ------------------------------------------------------------

#define WAL_MAGIC 0x57414C32  // "WAL2" in ASCII

// WAL format version
#define WAL_VERSION_V1 0x01 // wal may contain mixed versions after upgrades, so we need a version field in the header to distinguish them. This allows us to evolve the format in the future while maintaining backward compatibility with old records.
#define WAL_VERSION_V2 0x02

// ------------------------------------------------------------
// WAL Record Types (Transactional)
// ------------------------------------------------------------

// WAL record types 
//enum wal_record_type : uint8_t{ 
//     WAL_RECORD_PUT = 0X01, 
//     WAL_RECORD_DELETE = 0X02, 
// };
enum wal_record_type : uint8_t {
    WAL_TX_BEGIN  = 0x10, // can be any number 
    WAL_TX_PUT    = 0x11,
    WAL_TX_DELETE = 0x12,
    WAL_TX_COMMIT = 0x13,
};

struct wal_record_header_v1 { 
    uint32_t magic; // WAL_MAGIC 
    uint8_t version; // WAL_VERSION_V1 
    uint8_t type; // wal_record_type 
    uint32_t key_len; // length of key in bytes 
    uint32_t value_len; // length of value in bytes 
    };

// ------------------------------------------------------------
// Fixed-size WAL record header (v2)
// ------------------------------------------------------------
// This header precedes every record and defines its structure.
// All multi-byte integers are stored in little-endian format.
#pragma pack(push, 1)
struct wal_record_header_v2 {
    uint32_t magic;      // WAL_MAGIC
    uint8_t  version;    // WAL_VERSION_V2
    uint8_t  type;       // wal_record_type
    uint64_t txid;       // transaction identifier
    uint32_t key_len;    // key length (0 for BEGIN/COMMIT)
    uint32_t value_len;  // value length (0 for DELETE/BEGIN/COMMIT)
};
#pragma pack(pop)

// ------------------------------------------------------------
// Record layout on disk
// ------------------------------------------------------------
//
// | wal_record_header_v2 |
// | key bytes            |
// | value bytes          |
// | crc32 (uint32_t)     |
//
// CRC32 is computed over:
//   header (excluding crc field) +
//   key bytes +
//   value bytes
//
// The checksum is stored at the end to:
//   - Detect torn writes
//   - Detect bit corruption
//   - Guarantee full-record validation
//
// ------------------------------------------------------------
// Replay Semantics (Strict Prefix Model)
// ------------------------------------------------------------
//
// Recovery algorithm:
//
//   open WAL
//   while true:
//       read header
//       if EOF -> stop
//       validate magic and version
//       read payload
//       read crc
//       validate crc
//       if any step fails -> stop replay immediately
//
// Transaction rules:
//
//   - A transaction is valid iff its WAL_TX_COMMIT
//     record exists in the valid WAL prefix.
//
//   - WAL_TX_BEGIN has zero durability meaning.
//
//   - WAL_TX_PUT and WAL_TX_DELETE are applied
//     only if their txid has a valid COMMIT
//     within the prefix.
//
//   - Any transaction without COMMIT is ignored.
//
//   - No attempt is made to scan forward after corruption.
//     The first invalid record defines the end of truth.
//
// Durability rule:
//
//   A transaction is durable iff its WAL_TX_COMMIT
//   record has been appended and fsynced.
//
// Atomicity rule:
//
//   During recovery, either all operations of a committed
//   transaction are applied, or none are applied.
//
