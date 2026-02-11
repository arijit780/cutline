#pragma once
#include <cstdint>
#include <cstddef>
#include <functional>
#include <vector>

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

private:
    int fd_;
    const char* path_;
};
