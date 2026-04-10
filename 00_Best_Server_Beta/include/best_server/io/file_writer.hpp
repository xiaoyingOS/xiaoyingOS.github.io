// FileWriter - Asynchronous file writer with zero-copy support
// 
// Provides optimized file writing with:
// - Asynchronous I/O (io_uring on Linux, IOCP on Windows)
// - Zero-copy writing
// - Write batching
// - Buffer caching
// - Fsync optimization

#ifndef BEST_SERVER_IO_FILE_WRITER_HPP
#define BEST_SERVER_IO_FILE_WRITER_HPP

#include <string>
#include <memory>
#include <functional>
#include <system_error>
#include <queue>
#include <vector>

#include "io/io_event_loop.hpp"
#include "memory/zero_copy_buffer.hpp"

namespace best_server {
namespace io {

// Write callback
using WriteCallback = std::function<void(size_t, std::error_code)>;

// File writer statistics
struct FileWriterStats {
    uint64_t bytes_written{0};
    uint64_t write_count{0};
    uint64_t fsync_count{0};
    uint32_t open_files{0};
    uint32_t pending_writes{0};
};

// Asynchronous file writer
class FileWriter {
public:
    FileWriter();
    ~FileWriter();
    
    // Open a file
    bool open(const std::string& path, bool truncate = false);
    
    // Close the file
    void close();
    
    // Write data
    void write(memory::ZeroCopyBuffer&& data, WriteCallback callback);
    
    // Write data at specific offset
    void write_at(uint64_t offset, memory::ZeroCopyBuffer&& data, WriteCallback callback);
    
    // Append data
    void append(memory::ZeroCopyBuffer&& data, WriteCallback callback);
    
    // Flush pending writes
    void flush(WriteCallback callback = nullptr);
    
    // Sync file to disk
    void fsync(std::function<void(std::error_code)> callback);
    
    // Get current position
    uint64_t position() const;
    
    // Seek to position
    bool seek(uint64_t position);
    
    // Check if file is open
    bool is_open() const { return fd_ != -1; }
    
    // Get file path
    const std::string& path() const { return path_; }
    
    // Get statistics
    const FileWriterStats& stats() const { return stats_; }
    
    // Enable write batching
    void enable_batch_writes(bool enable, size_t batch_size = 64 * 1024);
    
    // Set sync policy
    enum class SyncPolicy {
        Never,      // Never sync
        OnClose,    // Sync on close
        OnWrite,    // Sync after each write
        Periodic    // Sync periodically
    };
    void set_sync_policy(SyncPolicy policy, uint32_t interval_ms = 1000);
    
    // Set event loop
    void set_event_loop(IOEventLoop* loop) { event_loop_ = loop; }
    
private:
    void handle_write_complete(WriteCallback callback, size_t bytes_written, std::error_code ec);
    void flush_batch();
    void handle_sync_complete(std::function<void(std::error_code)> callback, std::error_code ec);
    
    int fd_;
    std::string path_;
    uint64_t position_;
    
    IOEventLoop* event_loop_;
    
    FileWriterStats stats_;
    
    // Write batching
    bool batch_writes_enabled_;
    size_t batch_size_;
    memory::ZeroCopyBuffer batch_buffer_;
    
    // Pending writes
    struct PendingWrite {
        memory::ZeroCopyBuffer data;
        WriteCallback callback;
        uint64_t offset;
    };
    std::queue<PendingWrite> pending_writes_;
    
    // Sync policy
    SyncPolicy sync_policy_;
    uint32_t sync_interval_ms_;
    [[maybe_unused]] uint64_t last_sync_time_;
};

// File writer pool
class FileWriterPool {
public:
    FileWriterPool(size_t max_open_files = 100);
    ~FileWriterPool();
    
    // Get a file writer
    std::shared_ptr<FileWriter> get_writer(const std::string& path);
    
    // Return a file writer
    void return_writer(const std::string& path);
    
    // Flush all writers
    void flush_all();
    
    // Set event loop
    void set_event_loop(IOEventLoop* loop) { event_loop_ = loop; }
    
    // Get pool statistics
    size_t active_writers() const;
    size_t pending_writes() const;
    
private:
    IOEventLoop* event_loop_;
    [[maybe_unused]] size_t max_open_files_;
    
    std::unordered_map<std::string, std::shared_ptr<FileWriter>> writers_;
    mutable std::mutex mutex_;
};

} // namespace io
} // namespace best_server

#endif // BEST_SERVER_IO_FILE_WRITER_HPP