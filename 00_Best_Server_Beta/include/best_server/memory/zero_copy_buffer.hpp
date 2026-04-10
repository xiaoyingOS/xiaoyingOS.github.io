// ZeroCopyBuffer - Zero-copy buffer for efficient data transfer
// 
// Implements a zero-copy buffer system with:
// - Reference counting
// - Shared memory support
// - Network buffer management
// - Minimal data copying

#ifndef BEST_SERVER_MEMORY_ZERO_COPY_BUFFER_HPP
#define BEST_SERVER_MEMORY_ZERO_COPY_BUFFER_HPP

// Platform detection
#if defined(_WIN32) || defined(_WIN64)
    #define BEST_SERVER_PLATFORM_WINDOWS 1
#elif defined(__APPLE__) && defined(__MACH__)
    #define BEST_SERVER_PLATFORM_MACOS 1
#elif defined(__linux__)
    #define BEST_SERVER_PLATFORM_LINUX 1
#else
    #define BEST_SERVER_PLATFORM_UNKNOWN 1
#endif

#include <memory>
#include <vector>
#include <cstring>
#include <cstdint>
#include <atomic>
#include <stdexcept>

// On Linux, include system headers to get iovec definition
#if BEST_SERVER_PLATFORM_LINUX
    #include <sys/uio.h>
#endif

namespace best_server {
namespace memory {

// Buffer data with reference counting
class BufferData {
public:
    static constexpr size_t DEFAULT_CAPACITY = 64 * 1024; // 64KB
    
    BufferData(size_t capacity = DEFAULT_CAPACITY);
    ~BufferData();
    
    // Get raw pointer
    char* data() { return data_.get(); }
    const char* data() const { return data_.get(); }
    
    // Get capacity
    size_t capacity() const { return capacity_; }
    
    // Resize (may reallocate)
    bool resize(size_t new_capacity);
    
    // Reference counting
    void add_ref() { ref_count_.fetch_add(1, std::memory_order_relaxed); }
    void release() {
        if (ref_count_.fetch_sub(1, std::memory_order_acq_rel) == 1) {
            delete this;
        }
    }
    
    // Get reference count (for debugging)
    uint32_t ref_count() const { return ref_count_.load(std::memory_order_relaxed); }
    
private:
    std::unique_ptr<char[]> data_;
    size_t capacity_;
    std::atomic<uint32_t> ref_count_;
};

// Zero-copy buffer
class ZeroCopyBuffer {
public:
    ZeroCopyBuffer();
    explicit ZeroCopyBuffer(size_t capacity);
    ZeroCopyBuffer(const char* data, size_t size);  // Construct from existing data
    ~ZeroCopyBuffer();
    
    // Copy constructor (reference counting)
    ZeroCopyBuffer(const ZeroCopyBuffer& other);
    ZeroCopyBuffer& operator=(const ZeroCopyBuffer& other);
    
    // Move constructor
    ZeroCopyBuffer(ZeroCopyBuffer&& other) noexcept;
    ZeroCopyBuffer& operator=(ZeroCopyBuffer&& other) noexcept;
    
    // Get raw data
    char* data() { return data_.get() ? data_->data() + offset_ : nullptr; }
    const char* data() const { return data_.get() ? data_->data() + offset_ : nullptr; }
    
    // Get size
    size_t size() const { return size_; }
    
    // Get capacity
    size_t capacity() const { return data_.get() ? data_->capacity() - offset_ : 0; }
    
    // Check if empty
    bool empty() const { return size_ == 0; }
    
    // Get remaining space
    size_t remaining() const { return capacity() - size_; }
    
    // Write data
    bool write(const void* src, size_t len);
    
    // Read data
    bool read(void* dst, size_t len);
    
    // Consume data (advance read position)
    void consume(size_t len);
    
    // Clear buffer
    void clear();
    
    // Reserve space
    bool reserve(size_t len);
    
    // Append data from another buffer (optimized)
    bool append(const ZeroCopyBuffer& other);
    
    // Get a view of a portion of the buffer (no copy)
    ZeroCopyBuffer slice(size_t offset, size_t len) const;
    
    // Ensure unique ownership (copy-on-write, atomic check)
    void ensure_unique();
    
    // Check if buffer is uniquely owned (lock-free)
    bool is_unique() const;
    
private:
    std::shared_ptr<BufferData> data_;
    size_t offset_;
    size_t size_;
};

// Buffer chain for scatter/gather I/O
class BufferChain {
public:
    BufferChain();
    ~BufferChain();
    
    // Add a buffer to the chain
    void add_buffer(ZeroCopyBuffer&& buffer);
    
    // Get total size
    size_t total_size() const;
    
    // Get buffer count
    size_t buffer_count() const { return buffers_.size(); }
    
    // Access individual buffers
    ZeroCopyBuffer& operator[](size_t index) { return buffers_[index]; }
    const ZeroCopyBuffer& operator[](size_t index) const { return buffers_[index]; }
    
    // Flatten all buffers into one
    ZeroCopyBuffer flatten() const;
    
    // Clear the chain
    void clear();
    
    // Write data to the chain
    bool write(const void* src, size_t len);
    
    // Read data from the chain
    bool read(void* dst, size_t len);
    
    // Consume data
    void consume(size_t len);
    
private:
    std::vector<ZeroCopyBuffer> buffers_;
};

// I/O vector wrapper for scatter/gather
class IOVector {
public:
    static constexpr size_t MAX_IOV = 1024;
    
    IOVector();
    ~IOVector();
    
    // Add a buffer to the vector
    void add_buffer(const ZeroCopyBuffer& buffer);
    
    // Add a raw buffer
    void add_buffer(const void* data, size_t size);
    
    // Get iovec array
    const iovec* iov() const { return iov_; }
    
    // Get count
    size_t count() const { return count_; }
    
    // Clear
    void clear();
    
    // Get total size
    size_t total_size() const;
    
private:
    iovec iov_[MAX_IOV];
    size_t count_;
    std::vector<std::shared_ptr<BufferData>> buffers_;
};

} // namespace memory
} // namespace best_server

#endif // BEST_SERVER_MEMORY_ZERO_COPY_BUFFER_HPP