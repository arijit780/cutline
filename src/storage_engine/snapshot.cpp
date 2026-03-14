#include "snapshot.h"
#include <fstream>
#include <filesystem>

SnapshotManager::SnapshotManager(const std::string& directory)
    : dir_(directory) {}

std::string SnapshotManager::snapshot_path() const {
    return dir_ + "/snapshot.dat";
    // For simplicity, we use a fixed filename for the snapshot. In a real system, we might want to include the snapshot_lsn in the filename for better management of multiple snapshots.
}

std::string SnapshotManager::temp_snapshot_path() const {
    // Temporary file for atomic snapshot writing
    return dir_ + "/snapshot.tmp";
}

bool SnapshotManager::snapshot_exists() const {
    // Check if snapshot file exists
    std::ifstream file(snapshot_path(), std::ios::binary);
    return file.good();
}

bool SnapshotManager::write_snapshot(const MemTable* table, uint64_t snapshot_lsn) {
    std::ofstream out(temp_snapshot_path(), std::ios::binary | std::ios::trunc);
    if (!out.is_open())
        return false;

    // Write snapshot LSN
    out.write(reinterpret_cast<const char*>(&snapshot_lsn), sizeof(snapshot_lsn));

    // Write number of entries
    uint64_t count = table->size();
    out.write(reinterpret_cast<const char*>(&count), sizeof(count));

    // Serialize entries
    for (const auto& [key, entry] : *table) {

        uint64_t key_len = key.size();
        uint64_t val_len = entry.value.size();
        // Write key length and key data
        out.write(reinterpret_cast<const char*>(&key_len), sizeof(key_len));
        out.write(reinterpret_cast<const char*>(key.data()), key_len);
        // Write value length and value data
        out.write(reinterpret_cast<const char*>(&val_len), sizeof(val_len));
        out.write(reinterpret_cast<const char*>(entry.value.data()), val_len);
        // Write version
        out.write(reinterpret_cast<const char*>(&entry.version), sizeof(entry.version));
    }

    out.close();
    // Atomically replace old snapshot with new one
    std::rename(temp_snapshot_path().c_str(), snapshot_path().c_str());

    return true;
}

bool SnapshotManager::load_snapshot(MemTable& table, uint64_t& snapshot_lsn) {

    std::ifstream in(snapshot_path(), std::ios::binary);
    if (!in.is_open())
        return false;

    table.clear();

    // Read snapshot LSN
    in.read(reinterpret_cast<char*>(&snapshot_lsn), sizeof(snapshot_lsn));

    uint64_t count;
    in.read(reinterpret_cast<char*>(&count), sizeof(count));

    for (uint64_t i = 0; i < count; i++) {

        uint64_t key_len;
        uint64_t val_len;
        // Read key length and key data
        in.read(reinterpret_cast<char*>(&key_len), sizeof(key_len));
        std::vector<uint8_t> key(key_len);
        in.read(reinterpret_cast<char*>(key.data()), key_len);
        // Read value length and value data
        in.read(reinterpret_cast<char*>(&val_len), sizeof(val_len));
        std::vector<uint8_t> value(val_len);
        in.read(reinterpret_cast<char*>(value.data()), val_len);
        // Read version
        Version version;
        in.read(reinterpret_cast<char*>(&version), sizeof(version));
        // Insert into memtable
        table.emplace(std::move(key), ValueEntry{std::move(value), version});
    }

    return true;
}