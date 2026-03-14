#pragma once
#include <cstdint>

// ------------------------------------------------------------
// Snapshot File Format
// ------------------------------------------------------------
//
// SnapshotFile
// {
//     SnapshotHeader
//     SnapshotEntry entries[entry_count]
// }
//
// All integers stored little-endian.
//
// Invariant:
// snapshot_state == replay(WAL up to snapshot_lsn)
// ------------------------------------------------------------

// Format identification
#define SNAPSHOT_MAGIC   0x53534E50  // "SSNP"
#define SNAPSHOT_VERSION 0x01

#pragma pack(push, 1)

struct snapshot_header {
    uint32_t magic;        // SNAPSHOT_MAGIC
    uint8_t  version;      // SNAPSHOT_VERSION
    uint64_t snapshot_lsn; // WAL boundary
    uint64_t entry_count;  // number of KV entries following
};

#pragma pack(pop)

// Ensure header layout stays stable
// This is critical for binary compatibility of snapshot files across versions.
// this works by causing a compile-time error if the struct layout changes unexpectedly (e.g. due to padding or field reordering).
static_assert(sizeof(snapshot_header) == 21,
              "snapshot_header layout changed unexpectedly");