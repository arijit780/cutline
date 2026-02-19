#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <cerrno>
#include <cstring>
#include <stdexcept>
#include <vector>
#include <fstream>
#include <iostream>
#include <unordered_map>
#include <unordered_set>
#include "wal.h"
#include "wal_format.h"
using namespace std;

// ---------------- CRC32 helpers ----------------
static uint32_t crc32_update(uint32_t crc, const uint8_t* buf, size_t len) {
    static uint32_t table[256];
    static bool table_init = false;
    if (!table_init) {
        const uint32_t poly = 0xEDB88320u;
        for (uint32_t i = 0; i < 256; ++i) {
            uint32_t c = i;
            for (size_t j = 0; j < 8; ++j) {
                if (c & 1) c = poly ^ (c >> 1);
                else c = c >> 1;
            }
            table[i] = c;
        }
        table_init = true;
    }

    uint32_t r = crc ^ 0xFFFFFFFFu;
    for (size_t i = 0; i < len; ++i) {
        r = table[(r ^ buf[i]) & 0xFFu] ^ (r >> 8);
    }
    return r ^ 0xFFFFFFFFu;
}

static uint32_t compute_record_crc(const wal_record_header_v2& hdr, const uint8_t* payload, size_t payload_len) {
    uint32_t crc = 0u;
    crc = crc32_update(crc, reinterpret_cast<const uint8_t*>(&hdr), sizeof(hdr));
    if (payload_len > 0 && payload != nullptr) {
        crc = crc32_update(crc, payload, payload_len);
    }
    return crc;
}

// memcpy(destination, source, number_of_bytes);

LogRecord make_commit_record(uint64_t tx_id) {
    wal_record_header_v2 hdr;
    hdr.magic = WAL_MAGIC;
    hdr.version = WAL_VERSION;
    hdr.type = WAL_TX_COMMIT;
    hdr.txid = tx_id;
    hdr.key_len = 0;
    hdr.value_len = 0;

    uint32_t crc = compute_record_crc(hdr, nullptr, 0); 
    LogRecord rec;
    rec.bytes.resize(sizeof(hdr) + sizeof(crc));
    // Copy header at the beginning of the record
    memcpy(rec.bytes.data(), &hdr, sizeof(hdr));
    // Compute CRC (empty payload)
    crc = compute_record_crc(hdr, nullptr, 0);
    // Copy CRC at the end of the record
    memcpy(rec.bytes.data() + sizeof(hdr), &crc, sizeof(crc));
    return rec;
}


WriteAheadLog::WriteAheadLog(const char* path) : path_(path) {
    // write-only, create if not exists, append mode, with permissions 0644
    fd_ = ::open(path, O_WRONLY | O_CREAT | O_APPEND, 0644);
    if(fd_ < 0){
        throw runtime_error("Failed to open WAL file: " + string(strerror(errno)));
    }
    tx_active_ = false;
    current_tx_id_ = 0;
}

WriteAheadLog::~WriteAheadLog(){
    // ensure the file descriptor is closed when the WAL object is destroyed
    if(fd_ >= 0){
        ::close(fd_);
    }
}

void WriteAheadLog::append(const LogRecord& record){
    const uint8_t* bytes = record.bytes.data();//pointer to raw bytes
    size_t len = record.bytes.size();//total bytes to write
    size_t written = 0;// how many bytes the kernel accepted so far
    // Write entire record
    while(written < len){
        ssize_t rc = ::write(fd_, bytes + written, len - written);
        if(rc < 0){
            if(errno == EINTR) continue;// Retry on interrupt
            throw runtime_error("Failed to write to WAL: " + string(strerror(errno)));
        }
        written += static_cast<size_t>(rc);//Move forward by bytes actually written,Loop continues until full record is written
    }
}

void WriteAheadLog::flush(){
    // append already fsyncs
}
void WriteAheadLog::replay(const function<void(const LogRecord&)>& apply) {
    int fd = ::open(path_, O_RDONLY);// Open for reading
    if (fd < 0) {
        throw runtime_error("Failed to open WAL for replay: " + string(strerror(errno)));
    }

    // Collect pending records and track committed transactions
    std::unordered_map<uint64_t, std::vector<LogRecord>> pending;
    std::unordered_set<uint64_t> committed;

    // Scan phase: read and validate all records
    while (true) {
        wal_record_header_v2 hdr;

        // 1. Read header
        ssize_t n = ::read(fd, &hdr, sizeof(hdr));
        if (n == 0) {
            // Clean EOF...no more bytes to read, just stop
            break;
        }
        if (n != static_cast<ssize_t>(sizeof(hdr))) {
            // Partial header → corruption
            cerr << "Warning: Partial header read, stopping replay\n";
            break;
        }

        // 2. Validate magic
        if (hdr.magic != WAL_MAGIC) {
            // Corruption - stop replay
            cerr << "Warning: Invalid magic, stopping replay\n";
            break;
        }

        // 3. Validate version
        if (hdr.version != WAL_VERSION) {
            // Unknown format → stop
            cerr << "Warning: Unknown WAL version, stopping replay\n";
            break;
        }

        // 4. Validate payload sizes before allocating
        if (hdr.key_len > WAL_MAX_KEY_SIZE || hdr.value_len > WAL_MAX_VALUE_SIZE) {
            cerr << "Warning: Payload size exceeds limits (key=" << hdr.key_len 
                 << ", value=" << hdr.value_len << "), stopping replay\n";
            break;
        }

        size_t payload_len = hdr.key_len + hdr.value_len;
        vector<uint8_t> payload(payload_len);

        // 5. Read payload
        if (payload_len > 0) {
            ssize_t m = ::read(fd, payload.data(), payload_len);
            if (m != static_cast<ssize_t>(payload_len)) {
                cerr << "Warning: Torn write (expected " << payload_len << " bytes, got " << m << "), stopping replay\n";
                break;
            }
        }

        // 6. Read checksum
        uint32_t stored_crc;
        if (::read(fd, &stored_crc, sizeof(stored_crc)) != static_cast<ssize_t>(sizeof(stored_crc))) {
            cerr << "Warning: Failed to read CRC, stopping replay\n";
            break;
        }

        // 7. Verify checksum
        uint32_t computed_crc = compute_record_crc(hdr, payload.data(), payload_len);
        if (computed_crc != stored_crc) {
            cerr << "Warning: CRC mismatch (expected " << std::hex << computed_crc 
                 << ", got " << stored_crc << std::dec << "), stopping replay\n";
            break;
        }

        // 8. Reconstruct the record
        LogRecord rec;
        rec.bytes.resize(sizeof(hdr) + payload_len + sizeof(stored_crc));
        memcpy(rec.bytes.data(), &hdr, sizeof(hdr));
        if (payload_len > 0) {
            memcpy(rec.bytes.data() + sizeof(hdr), payload.data(), payload_len);
        }
        memcpy(rec.bytes.data() + sizeof(hdr) + payload_len, &stored_crc, sizeof(stored_crc));

        // 9. Collect record based on type
        if (hdr.type == WAL_TX_BEGIN) {
            // Start a new pending transaction
            if (pending.find(hdr.txid) == pending.end()) {
                pending[hdr.txid] = vector<LogRecord>();
            }
        } else if (hdr.type == WAL_TX_PUT || hdr.type == WAL_TX_DELETE) {
            // Add record to pending transaction
            if (pending.find(hdr.txid) != pending.end()) {
                pending[hdr.txid].push_back(rec);
            } else {
                // PUT/DELETE without BEGIN - skip or log warning
                cerr << "Warning: PUT/DELETE without BEGIN for txid " << hdr.txid << "\n";
            }
        } else if (hdr.type == WAL_TX_COMMIT) {
            // Mark transaction as committed
            committed.insert(hdr.txid);
        }
    }

    ::close(fd);

    // Apply phase: only apply records from committed transactions
    for (uint64_t txid : committed) {
        if (pending.find(txid) != pending.end()) {
            for (const LogRecord& rec : pending[txid]) {
                apply(rec);
            }
        }
    }
}


void WriteAheadLog::begun_tx(uint64_t txid) {
    // Construct a BEGIN record
    wal_record_header_v2 hdr;
    hdr.magic = WAL_MAGIC;
    hdr.version = WAL_VERSION;
    hdr.type = WAL_TX_BEGIN;
    hdr.key_len = 0;
    hdr.value_len = 0;
    hdr.txid = txid;
    uint32_t crc = compute_record_crc(hdr, nullptr, 0); // No payload for BEGIN 
    LogRecord rec;
    // Allocate bytes for header + crc (no payload)
    rec.bytes.resize(sizeof(hdr) + sizeof(crc));
    // Copy header at the beginning of the record
    memcpy(rec.bytes.data(), &hdr, sizeof(hdr));
    // Copy CRC at the end of the record
    memcpy(rec.bytes.data() + sizeof(hdr), &crc, sizeof(crc));

    append(rec);
}



void WriteAheadLog::tx_put(uint64_t tx_id, const std::vector<uint8_t>& key, const std::vector<uint8_t>& value) {
    wal_record_header_v2 hdr;
    hdr.magic = WAL_MAGIC;
    hdr.version = WAL_VERSION;
    hdr.type = WAL_TX_PUT;
    hdr.txid = tx_id;
    hdr.key_len = static_cast<uint32_t>(key.size());
    hdr.value_len = static_cast<uint32_t>(value.size());

    size_t payload_len = key.size() + value.size();
    std::vector<uint8_t> payload(payload_len);
    memcpy(payload.data(), key.data(), key.size());
    memcpy(payload.data() + key.size(), value.data(), value.size());

    uint32_t crc = compute_record_crc(hdr, payload.data(), payload_len);

    LogRecord rec;
    rec.bytes.resize(sizeof(hdr) + payload_len + sizeof(crc));
    memcpy(rec.bytes.data(), &hdr, sizeof(hdr));
    memcpy(rec.bytes.data() + sizeof(hdr), payload.data(), payload_len);
    memcpy(rec.bytes.data() + sizeof(hdr) + payload_len, &crc, sizeof(crc));

    append(rec);
}

void WriteAheadLog::tx_delete(uint64_t tx_id, const std::vector<uint8_t>& key) {
    wal_record_header_v2 hdr;
    hdr.magic = WAL_MAGIC;
    hdr.version = WAL_VERSION;
    hdr.type = WAL_TX_DELETE;
    hdr.txid = tx_id;
    hdr.key_len = static_cast<uint32_t>(key.size());
    hdr.value_len = 0;

    size_t payload_len = key.size();

    uint32_t crc = compute_record_crc(hdr, key.data(), payload_len);

    LogRecord rec;
    rec.bytes.resize(sizeof(hdr) + payload_len + sizeof(crc));
    memcpy(rec.bytes.data(), &hdr, sizeof(hdr));
    // Copy key as payload (no value for DELETE)
    memcpy(rec.bytes.data() + sizeof(hdr), key.data(), payload_len);
    // Copy CRC at the end of the record
    memcpy(rec.bytes.data() + sizeof(hdr) + payload_len, &crc, sizeof(crc));
    // Append the record to the WAL
    append(rec);
}

void WriteAheadLog::commit_tx(uint64_t tx_id) {
    LogRecord rec = make_commit_record(tx_id);
    append(rec);
    if(::fsync(fd_)<0){
        throw runtime_error("Failed to fsync WAL after commit: " + string(strerror(errno)));
    }
}

