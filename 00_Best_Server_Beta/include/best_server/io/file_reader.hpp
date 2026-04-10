// FileReader - Asynchronous file reader with zero-copy support
// 
// Provides optimized file reading with:
// - Asynchronous I/O (io_uring on Linux, IOCP on Windows)
// - Zero-copy reading
// - Memory mapping support
// - Prefetching
// - Buffer caching

#ifndef BEST_SERVER_IO_FILE_READER_HPP
#define BEST_SERVER_IO_FILE_READER_HPP

#include <string>
#include <memory>
#include <functional>
#include <system_error>
#include <cstdint>
#include <queue>

#include "io/io_event_loop.hpp"
#include "memory/zero_copy_buffer.hpp"

namespace best_server {
namespace io {

// Read callback
using ReadCallback = std::function<void(memory::ZeroCopyBuffer&&, std::error_code)>;

// File reader statistics
struct FileReaderStats {
    uint64_t bytes_read{0};
    uint64_t read_count{0};
    uint64_t cache_hits{0};
    uint64_t cache_misses{0};
    uint32_t open_files{0};
};

// Asynchronous file reader
class FileReader {
public:
    FileReader();
    ~FileReader();
    
    // Open a file
    bool open(const std::string& path);
    
    // Close the file
    void close();
    
    // Read from current position
    void read(size_t size, ReadCallback callback);
    
    // Read from specific offset
    void read_at(uint64_t offset, size_t size, ReadCallback callback);
    
    // Read entire file
    void read_all(ReadCallback callback);
    
    // Prefetch data (async read ahead)
    void prefetch(uint64_t offset, size_t size);
    
    // Get file size
    uint64_t size() const;
    
    // Get current position
    uint64_t position() const;
    
    // Seek to position
    bool seek(uint64_t position);
    
    // Check if file is open
    bool is_open() const { return fd_ != -1; }
    
    // Get file path
    const std::string& path() const { return path_; }
    
    // Get statistics
    const FileReaderStats& stats() const { return stats_; }
    
    // Enable memory mapping
    void enable_mmap(bool enable);
    
    // Set read-ahead size
    void set_read_ahead_size(size_t size);
    
    // Set event loop
    void set_event_loop(IOEventLoop* loop) { event_loop_ = loop; }
    
private:
    void handle_read_complete(memory::ZeroCopyBuffer&& buffer, std::error_code ec);
    void handle_read_at_complete(uint64_t offset, memory::ZeroCopyBuffer&& buffer, std::error_code ec);
    
    int fd_;
    std::string path_;
    uint64_t file_size_;
    uint64_t position_;
    
    IOEventLoop* event_loop_;
    
    FileReaderStats stats_;
    
    bool mmap_enabled_;
    size_t read_ahead_size_;
    
    // Cache for frequently accessed data
    struct CacheEntry {
        uint64_t offset;
        memory::ZeroCopyBuffer data;
        uint64_t last_access;
    };
    std::unordered_map<uint64_t, CacheEntry> cache_;
    size_t cache_size_;
};

// File reader pool (for reading multiple files concurrently)
class FileReaderPool {
public:
    FileReaderPool(size_t max_open_files = 100);
    ~FileReaderPool();
    
    // Get a file reader
    std::shared_ptr<FileReader> get_reader(const std::string& path);
    
    // Return a file reader
    void return_reader(const std::string& path);
    
    // Set event loop
    void set_event_loop(IOEventLoop* loop) { event_loop_ = loop; }
    
    // Clear cache
    void clear_cache();
    
    // Get pool statistics
    size_t active_readers() const;
    size_t cached_readers() const;
    
private:
    IOEventLoop* event_loop_;
    size_t max_open_files_;
    
    std::unordered_map<std::string, std::shared_ptr<FileReader>> readers_;
    std::deque<std::string> lru_queue_;
    mutable std::mutex mutex_;
};

} // namespace io
} // namespace best_server

#endif // BEST_SERVER_IO_FILE_READER_HPP