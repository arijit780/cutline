#pragma once
#include <cstdint>
#include <cstddef>
#include <functional>
#include <vector>
#include <unordered_set>

struct LogRecord {
    std::vector<uint8_t> bytes;
};

class WriteAheadLog {
public:
    explicit WriteAheadLog(const char* path);
    ~WriteAheadLog();

    // Append a log record to the WAL. This should be atomic and durable.
    void append(const LogRecord& record);

    // Flush any pending writes (append already fsyncs)
    void flush();

    // Replay all records from the WAL file
    void replay(const std::function<void(const LogRecord&)>& apply);
    // Helper methods for constructing transactional records
    void begun_tx(uint64_t txid);

    void tx_put(uint64_t tx_id, const std::vector<uint8_t>& key, const std::vector<uint8_t>& value);

    void tx_delete(uint64_t tx_id, const std::vector<uint8_t>& key);

    void commit_tx(uint64_t tx_id);

private:
    int fd_;
    const char* path_;
    bool tx_active_;
    uint64_t current_tx_id_;
    std::unordered_set<uint64_t> active_txs_; // Track active transactions for validation during replay
};

// Helper to create a commit record for a transaction
LogRecord make_commit_record(uint64_t tx_id);
