#pragma once

#include <string>
#include <cstdint>
#include "memtable.h"
#include "snapshot_format.h"

// ------------------------------------------------------------
// Snapshot Manager
// ------------------------------------------------------------
//
// Responsible for:
//   - writing snapshots to disk
//   - loading snapshots during recovery
//
// Snapshot invariant:
// snapshot_state == replay(WAL up to snapshot_lsn)
//

class SnapshotManager {
public:
    explicit SnapshotManager(const std::string& directory);

    // Serialize immutable memtable to disk snapshot
    bool write_snapshot(const MemTable* table, uint64_t snapshot_lsn);

    // Load snapshot from disk into memtable
    bool load_snapshot(MemTable& table, uint64_t& snapshot_lsn);

    // Check whether snapshot exists
    bool snapshot_exists() const;

private:
    std::string dir_;

    std::string snapshot_path() const;
    std::string temp_snapshot_path() const;
};