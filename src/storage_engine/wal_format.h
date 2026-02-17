#pragma once
#include <cstdint>

// ------------------------------------------------------------
// WAL Invariants
// ------------------------------------------------------------
// - Records are strictly append-only
// - Records are self-delimiting
// - Records are checksummed
// - Replay is sequential and prefix-based
// - First corrupted record terminates replay
// - A transaction exists iff its COMMIT record exists
// - BEGIN has zero durability meaning

// ------------------------------------------------------------
// Constants
// ------------------------------------------------------------

#define WAL_MAGIC        0x57414C32  // "WAL2"
#define WAL_VERSION      0x02

// Maximum allowed payload sizes (defensive)
#define WAL_MAX_KEY_SIZE     (1 << 20)   // 1MB
#define WAL_MAX_VALUE_SIZE   (1 << 24)   // 16MB

// ------------------------------------------------------------
// Record Types (Transactional)
// ------------------------------------------------------------

enum wal_record_type : uint8_t {
    WAL_TX_BEGIN  = 0x10,
    WAL_TX_PUT    = 0x11,
    WAL_TX_DELETE = 0x12,
    WAL_TX_COMMIT = 0x13,
};

// ------------------------------------------------------------
// WAL Record Header (v2)
// ------------------------------------------------------------
// All integers are stored in little-endian format.
// Header does NOT include CRC.
// CRC32 follows payload.

#pragma pack(push, 1)
struct wal_record_header_v2 {
    uint32_t magic;      // WAL_MAGIC
    uint8_t  version;    // WAL_VERSION
    uint8_t  type;       // wal_record_type
    uint64_t txid;       // transaction identifier
    uint32_t key_len;    // key length in bytes
    uint32_t value_len;  // value length in bytes
};
#pragma pack(pop)

// Compile-time check: header size must remain fixed
static_assert(sizeof(wal_record_header_v2) == 22,
              "WAL header size changed unexpectedly");
