#include "snapshot.h"
#include "snapshot_format.h"

#include <fstream>
#include <cstdio>

SnapshotManager::SnapshotManager(const std::string& directory)
    : dir_(directory) {}

std::string SnapshotManager::snapshot_path() const {
    return dir_ + "/snapshot.dat";
}

std::string SnapshotManager::temp_snapshot_path() const {
    return dir_ + "/snapshot.tmp";
}

bool SnapshotManager::snapshot_exists() const {
    // Check if snapshot file exists by trying to open it
    std::ifstream file(snapshot_path(), std::ios::binary);
    return file.good();
}

bool SnapshotManager::write_snapshot(const MemTable* table, uint64_t snapshot_lsn) {
    // Write to a temporary file first to ensure atomicity
    std::ofstream out(temp_snapshot_path(), std::ios::binary | std::ios::trunc);
    if (!out.is_open())
        return false;

    // ---- Write snapshot header ----
    snapshot_header header;
    header.magic        = SNAPSHOT_MAGIC;
    header.version      = SNAPSHOT_VERSION;
    header.snapshot_lsn = snapshot_lsn;
    header.entry_count  = table->size();
    
    // Write header to file(append raw bytes of header struct to file) which will be read back during loading to validate and understand the snapshot file structure
    out.write(reinterpret_cast<const char*>(&header), sizeof(header));

    // ---- Serialize entries ----
    for (const auto& [key, entry] : *table) {
        // Serialize each entry as: [key_len][key_bytes][value_len][value_bytes][version]
        uint64_t key_len = key.size();
        uint64_t val_len = entry.value.size();

        out.write(reinterpret_cast<const char*>(&key_len), sizeof(key_len));
        out.write(reinterpret_cast<const char*>(key.data()), key_len);

        out.write(reinterpret_cast<const char*>(&val_len), sizeof(val_len));
        out.write(reinterpret_cast<const char*>(entry.value.data()), val_len);

        out.write(reinterpret_cast<const char*>(&entry.version),
                  sizeof(entry.version));
    }

    out.close();

    // atomic replace
    std::rename(temp_snapshot_path().c_str(), snapshot_path().c_str());

    return true;
}

bool SnapshotManager::load_snapshot(MemTable& table, uint64_t& snapshot_lsn) {

    std::ifstream in(snapshot_path(), std::ios::binary);
    if (!in.is_open())
        return false;

    table.clear();

    // ---- Read snapshot header ----
    snapshot_header header;

    in.read(reinterpret_cast<char*>(&header), sizeof(header));
    if (!in)
        return false;

    if (header.magic != SNAPSHOT_MAGIC)
        return false;

    if (header.version != SNAPSHOT_VERSION)
        return false;

    snapshot_lsn = header.snapshot_lsn;

    // ---- Load entries ----
    for (uint64_t i = 0; i < header.entry_count; i++) {

        uint64_t key_len;
        uint64_t val_len;

        in.read(reinterpret_cast<char*>(&key_len), sizeof(key_len));
        std::vector<uint8_t> key(key_len);
        in.read(reinterpret_cast<char*>(key.data()), key_len);

        in.read(reinterpret_cast<char*>(&val_len), sizeof(val_len));
        std::vector<uint8_t> value(val_len);
        in.read(reinterpret_cast<char*>(value.data()), val_len);

        Version version;
        in.read(reinterpret_cast<char*>(&version), sizeof(version));

        table.emplace(std::move(key), ValueEntry{std::move(value), version});
    }

    return true;
}