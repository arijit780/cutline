#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <cerrno>
#include <cstring>
#include <stdexcept>
#include <vector>
#include <fstream>
#include <iostream>
using namespace std;

struct LogRecord {
    vector<uint8_t> bytes;//represents one fully serialized wal record, including header, payload, and crc. This is the unit of data that will be written to the WAL file.
};

class WAL {
public:
    explicit WAL(const char* path){//explicit prevents accidental implicit conversions
        // write-only, create if not exists, append mode, with permissions 0644 owner read/write, group read, others read
        fd_ = ::open(path,O_WRONLY | O_CREAT | O_APPEND, 0644);
        if(fd_ < 0){
            throw runtime_error("Failed to open WAL file: " + string(strerror(errno)));
        }
    }
    ~WAL(){
        // ensure the file descriptor is closed when the WAL object is destroyed to prevent resource leaks
        if(fd_ >= 0){
            ::close(fd_);
        }
    }

    // Append a log record to the WAL. This should be atomic and durable.
    void append(const LogRecord& record){
        const uint8_t* bytes = record.bytes.data();//pointer to raw bytes
        size_t len = record.bytes.size();//total bytes to write
        size_t written = 0; // how many bytes the kernel accepted so far
        // Write entire record
        while(written < len){
            ssize_t rc = ::write(fd_, bytes + written, len - written);
            if(rc < 0){
                if(errno == EINTR) continue; // Retry on interrupt
                throw runtime_error("Failed to write to WAL: " + string(strerror(errno)));
            }
            written += static_cast<size_t>(rc);//Move forward by bytes actually written,Loop continues until full record is written
        }

        // fsync to ensure durability
        if(::fsync(fd_) < 0){// call also happens here
            throw runtime_error("Failed to fsync WAL: " + string(strerror(errno)));
        } 
    }
    void flush(){
        // append already fsyns
    }
private:
    int fd_;
};


