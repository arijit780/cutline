#pragma once

#include <string>
#include <cstdint>
#include "memtable.h"

// SnapshotManager is responsible only for
//   - writing snapshots to disk
//   - loading snapshots during recovery
//
// Snapshot invariant:
//
//   snapshot_state == replay(WAL up to snapshot_lsn)
//
// snapshot_lsn marks the WAL position that the snapshot represents.

class SnapshotManager {
public:
    explicit SnapshotManager(const std::string& directory);

    // Write a snapshot of the provided immutable memtable
    // snapshot_lsn identifies the WAL boundary for this snapshot.
    bool write_snapshot(const MemTable* table, uint64_t snapshot_lsn);

    // Load snapshot from disk.
    // Reconstructs memtable contents and returns snapshot_lsn.
    bool load_snapshot(MemTable& table, uint64_t& snapshot_lsn);

    // Check whether a snapshot file exists
    bool snapshot_exists() const;

private:
    std::string dir_;

    std::string snapshot_path() const;
    std::string temp_snapshot_path() const;
};