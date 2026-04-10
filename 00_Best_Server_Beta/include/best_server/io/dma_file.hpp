// DMA File Reader - Zero-copy DMA-based file I/O
//
// Implements DMA-based file I/O for high-performance storage access:
// - Direct memory access
// - Zero-copy file reading
// - Asynchronous I/O
// - Scatter/gather support
// - Memory-mapped file access

#ifndef BEST_SERVER_IO_DMA_FILE_HPP
#define BEST_SERVER_IO_DMA_FILE_HPP

#include "best_server/future/future.hpp"
#include "best_server/memory/zero_copy_buffer.hpp"
#include <string>
#include <memory>
#include <atomic>

#if BEST_SERVER_PLATFORM_LINUX
    #include <libaio.h>
#endif

namespace best_server {
namespace io {

// DMA file handle
class DMAFile {
public:
    DMAFile();
    ~DMAFile();
    
    // Open file
    bool open(const std::string& path, bool read_only = true);
    
    // Close file
    void close();
    
    // Check if open
    bool is_open() const { return fd_ >= 0; }
    
    // Get file descriptor
    int fd() const { return fd_; }
    
    // Get file size
    size_t size() const { return file_size_; }
    
    // Pre-allocate space
    bool preallocate(size_t size);
    
    // Enable O_DIRECT (DMA)
    bool enable_direct_io(bool enable);
    
private:
    int fd_;
    size_t file_size_;
    bool direct_io_enabled_;
};

// DMA file reader
class DMAFileReader {
public:
    static constexpr size_t MAX_AIO_EVENTS = 256;
    static constexpr size_t DEFAULT_BLOCK_SIZE = 4096;  // 4KB aligned
    
    DMAFileReader();
    ~DMAFileReader();
    
    // Initialize AIO context
    bool initialize(size_t max_events = MAX_AIO_EVENTS);
    
    // Read async (DMA)
    future::Future<memory::ZeroCopyBuffer> read_async(
        DMAFile* file,
        size_t offset,
        size_t size
    );
    
    // Read batch async (DMA)
    future::Future<std::vector<memory::ZeroCopyBuffer>> read_batch_async(
        DMAFile* file,
        const std::vector<std::pair<size_t, size_t>>& requests
    );
    
    // Wait for completion
    void wait_for_completion();
    
    // Get pending count
    size_t pending_count() const;
    
private:
    struct IORequest {
        size_t offset;
        size_t size;
        memory::ZeroCopyBuffer buffer;
        future::Promise<memory::ZeroCopyBuffer> promise;
        bool completed;
    };
    
#if BEST_SERVER_PLATFORM_LINUX
    io_context_t aio_ctx_;
#endif
    std::vector<IORequest> pending_requests_;
    std::atomic<size_t> pending_count_;
    bool initialized_;
};

// DMA file writer
class DMAFileWriter {
public:
    static constexpr size_t MAX_AIO_EVENTS = 256;
    static constexpr size_t DEFAULT_BLOCK_SIZE = 4096;  // 4KB aligned
    
    DMAFileWriter();
    ~DMAFileWriter();
    
    // Initialize AIO context
    bool initialize(size_t max_events = MAX_AIO_EVENTS);
    
    // Write async (DMA)
    future::Future<size_t> write_async(
        DMAFile* file,
        size_t offset,
        const memory::ZeroCopyBuffer& data
    );
    
    // Write batch async (DMA)
    future::Future<size_t> write_batch_async(
        DMAFile* file,
        size_t base_offset,
        const std::vector<memory::ZeroCopyBuffer>& data
    );
    
    // Sync to disk
    bool sync(DMAFile* file);
    
    // Wait for completion
    void wait_for_completion();
    
private:
    struct IOWriteRequest {
        size_t offset;
        memory::ZeroCopyBuffer data;
        future::Promise<size_t> promise;
        bool completed;
    };
    
#if BEST_SERVER_PLATFORM_LINUX
    io_context_t aio_ctx_;
#endif
    std::vector<IOWriteRequest> pending_requests_;
    std::atomic<size_t> pending_count_;
    bool initialized_;
};

// Memory-mapped file (zero-copy alternative)
class MappedFile {
public:
    MappedFile();
    ~MappedFile();
    
    // Map file
    bool map(const std::string& path, bool read_only = true);
    
    // Unmap file
    void unmap();
    
    // Check if mapped
    bool is_mapped() const { return data_ != nullptr; }
    
    // Get mapped data
    const void* data() const { return data_; }
    void* data() { return data_; }
    
    // Get size
    size_t size() const { return size_; }
    
    // Sync to disk
    bool sync();
    
    // Advise on access pattern
    bool advise(int advice);
    
private:
    void* data_;
    size_t size_;
    int fd_;
    bool read_only_;
};

// Zero-copy file read using mmap
class ZeroCopyFileReader {
public:
    ZeroCopyFileReader();
    ~ZeroCopyFileReader();
    
    // Open file
    bool open(const std::string& path);
    
    // Close file
    void close();
    
    // Read (zero-copy, returns view)
    memory::ZeroCopyBuffer read(size_t offset, size_t size);
    
    // Get file size
    size_t size() const { return file_size_; }
    
private:
    MappedFile mapped_file_;
    size_t file_size_;
};

} // namespace io
} // namespace best_server

#endif // BEST_SERVER_IO_DMA_FILE_HPP