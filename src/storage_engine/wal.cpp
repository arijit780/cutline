#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <cerrno>
#include <cstring>
#include <stdexcept>
#include <vector>
#include <fstream>
#include <iostream>
#include "wal.h"
#include "wal_format.h"
using namespace std;

WriteAheadLog::WriteAheadLog(const char* path) : path_(path) {
    // write-only, create if not exists, append mode, with permissions 0644
    fd_ = ::open(path, O_WRONLY | O_CREAT | O_APPEND, 0644);
    if(fd_ < 0){
        throw runtime_error("Failed to open WAL file: " + string(strerror(errno)));
    }
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
    // fsync to ensure durability
    if(::fsync(fd_) < 0){// call also happens here
        throw runtime_error("Failed to fsync WAL: " + string(strerror(errno)));
    } 
}

void WriteAheadLog::flush(){
    // append already fsyncs
}
// Storage engine logic lives in apply
void WriteAheadLog::replay(const function<void(const LogRecord&)>& apply) {
    int fd = ::open(path_, O_RDONLY);// Open for reading
    if (fd < 0) {
        throw runtime_error("Failed to open WAL for replay: " + string(strerror(errno)));
    }

    while (true) {
        wal_record_header_v1 hdr;

        // 1. Read header
        ssize_t n = ::read(fd, &hdr, sizeof(hdr));
        if (n == 0) {
            // Clean EOF...no more bytes to read, just stop
            break;
        }
        if (n != static_cast<ssize_t>(sizeof(hdr))) {
            // Partial header → corruption
            break;
        }

        // 2. Validate magic
        if (hdr.magic != WAL_MAGIC) {
            // Corruption - could skip to next magic, but for now just break
            break;
        }

        // 3. Validate version
        if (hdr.version != WAL_VERSION_V1) {
            // Unknown format → stop
            break;
        }

        // 4. Read payload
        size_t payload_len = hdr.key_len + hdr.value_len;//compute total payload length from header fields
        vector<uint8_t> payload(payload_len);//allocate buffer to hold payload

        ssize_t m = ::read(fd, payload.data(), payload_len);//this line attempts to read the entire payload of the WAL record into memory, and the return value tells you whether the record is fully present or torn.
        if (m != static_cast<ssize_t>(payload_len)) {
            // Torn write
            break;
        }

        // 5. Read checksum
        uint32_t stored_crc;
        if (::read(fd, &stored_crc, sizeof(stored_crc)) != static_cast<ssize_t>(sizeof(stored_crc))) {
            break;
        }

        // 6. For now, skip checksum verification (TODO: implement CRC32)
        // Just reconstruct and apply the record
        LogRecord rec;
        rec.bytes.resize(sizeof(hdr) + payload_len + sizeof(stored_crc));// allocate bytes for full record
        memcpy(rec.bytes.data(), &hdr, sizeof(hdr));// copy header into record bytes
        memcpy(rec.bytes.data() + sizeof(hdr), payload.data(), payload_len);// copy payload into record bytes
        memcpy(rec.bytes.data() + sizeof(hdr) + payload_len, &stored_crc, sizeof(stored_crc));  // copy crc into record bytes

        // 7. Apply record
        apply(rec);// apply record to MemTable. this logic lies outside of the wal
    }

    ::close(fd);
}


