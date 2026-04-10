// Cache Optimization - Cache-line alignment and optimization utilities
//
// Provides utilities for:
// - Cache-line alignment (64 bytes)
// - Cache-friendly data structures
// - Hot/cold data separation
// - Padding to avoid false sharing

#ifndef BEST_SERVER_CORE_CACHE_OPTIMIZATION_HPP
#define BEST_SERVER_CORE_CACHE_OPTIMIZATION_HPP

#include <cstddef>
#include <cstdint>
#include <array>

namespace best_server {
namespace core {

// Cache line size (typical modern CPU)
constexpr size_t CACHE_LINE_SIZE = 64;

// Round up to cache line size
constexpr size_t round_up_to_cache_line(size_t size) {
    return (size + CACHE_LINE_SIZE - 1) / CACHE_LINE_SIZE * CACHE_LINE_SIZE;
}

// Check if size is cache-line aligned
constexpr bool is_cache_line_aligned(size_t size) {
    return (size % CACHE_LINE_SIZE) == 0;
}

// Cache-line aligned buffer
template<size_t Size>
class CacheLineAlignedBuffer {
public:
    static_assert(Size > 0, "Size must be positive");
    
    // Get buffer size (padded to cache line)
    static constexpr size_t buffer_size = round_up_to_cache_line(Size);
    
    CacheLineAlignedBuffer() : data_{} {
        // Initialize buffer to zero
        for (size_t i = 0; i < buffer_size; ++i) {
            data_[i] = 0;
        }
    }
    
    // Get raw pointer
    uint8_t* data() { return data_; }
    const uint8_t* data() const { return data_; }
    
    // Get size
    static constexpr size_t size() { return Size; }
    static constexpr size_t aligned_size() { return buffer_size; }
    
    // Access as array
    uint8_t& operator[](size_t index) {
        return data_[index];
    }
    
    const uint8_t& operator[](size_t index) const {
        return data_[index];
    }
    
    // Clear buffer
    void clear() {
        for (size_t i = 0; i < buffer_size; ++i) {
            data_[i] = 0;
        }
    }
    
private:
    alignas(CACHE_LINE_SIZE) uint8_t data_[buffer_size];
};

// Cache-line aligned atomic counter (avoid false sharing)
template<typename T>
class CacheLineAlignedCounter {
public:
    CacheLineAlignedCounter() : value_(0) {}
    
    // Increment (relaxed)
    void inc(T delta = 1) {
        value_.fetch_add(delta, std::memory_order_relaxed);
    }
    
    // Decrement (relaxed)
    void dec(T delta = 1) {
        value_.fetch_sub(delta, std::memory_order_relaxed);
    }
    
    // Get value (relaxed)
    T get() const {
        return value_.load(std::memory_order_relaxed);
    }
    
    // Set value (relaxed)
    void set(T new_value) {
        value_.store(new_value, std::memory_order_relaxed);
    }
    
    // Atomic exchange
    T exchange(T new_value) {
        return value_.exchange(new_value, std::memory_order_relaxed);
    }
    
    // Compare and set
    bool compare_exchange(T& expected, T desired) {
        return value_.compare_exchange_strong(expected, desired, 
                                             std::memory_order_acq_rel,
                                             std::memory_order_relaxed);
    }
    
private:
    alignas(CACHE_LINE_SIZE) std::atomic<T> value_;
    // Padding to prevent false sharing
    uint8_t padding_[CACHE_LINE_SIZE - sizeof(std::atomic<T>)];
};

// Cache-line aligned atomic flag
class CacheLineAlignedFlag {
public:
    CacheLineAlignedFlag() : flag_(false) {}
    
    // Set flag (relaxed)
    void set(bool value = true) {
        flag_.store(value, std::memory_order_relaxed);
    }
    
    // Get flag (relaxed)
    bool get() const {
        return flag_.load(std::memory_order_relaxed);
    }
    
    // Test and set (acquire)
    bool test_and_set() {
        return flag_.exchange(true, std::memory_order_acquire);
    }
    
    // Clear flag (release)
    void clear() {
        flag_.store(false, std::memory_order_release);
    }
    
private:
    alignas(CACHE_LINE_SIZE) std::atomic<bool> flag_;
    // Padding to prevent false sharing
    [[maybe_unused]] uint8_t padding_[CACHE_LINE_SIZE - sizeof(std::atomic<bool>)];
};

// Cache-friendly small vector (size <= 32, fits in cache line)
template<typename T, size_t Capacity>
class CacheFriendlySmallVector {
public:
    static_assert(Capacity <= 32, "Capacity must be <= 32 to fit in cache line");
    static_assert(sizeof(T) * Capacity <= CACHE_LINE_SIZE, "Total size must fit in cache line");
    
    CacheFriendlySmallVector() : size_(0) {}
    
    // Push back (no bounds checking for performance)
    void push_back(const T& value) {
        data_[size_++] = value;
    }
    
    void push_back(T&& value) {
        data_[size_++] = std::move(value);
    }
    
    // Pop back
    void pop_back() {
        --size_;
    }
    
    // Access
    T& operator[](size_t index) { return data_[index]; }
    const T& operator[](size_t index) const { return data_[index]; }
    
    // Front/Back
    T& front() { return data_[0]; }
    const T& front() const { return data_[0]; }
    T& back() { return data_[size_ - 1]; }
    const T& back() const { return data_[size_ - 1]; }
    
    // Size
    size_t size() const { return size_; }
    static constexpr size_t capacity() { return Capacity; }
    
    // Empty
    bool empty() const { return size_ == 0; }
    
    // Full
    bool full() const { return size_ == Capacity; }
    
    // Clear
    void clear() { size_ = 0; }
    
private:
    alignas(CACHE_LINE_SIZE) std::array<T, Capacity> data_;
    size_t size_;
};

// Cache-padded pointer (prevent false sharing)
template<typename T>
class CachePaddedPointer {
public:
    CachePaddedPointer() : ptr_(nullptr) {}
    CachePaddedPointer(T* ptr) : ptr_(ptr) {}
    
    // Get pointer
    T* get() const { return ptr_; }
    
    // Set pointer
    void set(T* ptr) { ptr_ = ptr; }
    
    // Arrow operator
    T* operator->() const { return ptr_; }
    
    // Dereference operator
    T& operator*() const { return *ptr_; }
    
    // Conversion to bool
    explicit operator bool() const { return ptr_ != nullptr; }
    
private:
    alignas(CACHE_LINE_SIZE) T* ptr_;
    // Padding to prevent false sharing
    uint8_t padding_[CACHE_LINE_SIZE - sizeof(T*)];
};

// Cache-optimized ring buffer (power of 2 size for fast modulo)
template<typename T, size_t Capacity>
class CacheOptimizedRingBuffer {
public:
    static_assert((Capacity & (Capacity - 1)) == 0, "Capacity must be power of 2");
    
    CacheOptimizedRingBuffer() : head_(0), tail_(0) {}
    
    // Push (no bounds checking)
    bool push(const T& value) {
        size_t next_tail = (tail_ + 1) & (Capacity - 1);
        if (next_tail == head_) {
            return false; // Full
        }
        buffer_[tail_] = value;
        tail_ = next_tail;
        return true;
    }
    
    bool push(T&& value) {
        size_t next_tail = (tail_ + 1) & (Capacity - 1);
        if (next_tail == head_) {
            return false; // Full
        }
        buffer_[tail_] = std::move(value);
        tail_ = next_tail;
        return true;
    }
    
    // Pop
    bool pop(T& value) {
        if (head_ == tail_) {
            return false; // Empty
        }
        value = std::move(buffer_[head_]);
        head_ = (head_ + 1) & (Capacity - 1);
        return true;
    }
    
    // Peek
    T* peek() {
        if (head_ == tail_) {
            return nullptr;
        }
        return &buffer_[head_];
    }
    
    const T* peek() const {
        if (head_ == tail_) {
            return nullptr;
        }
        return &buffer_[head_];
    }
    
    // Size
    size_t size() const {
        return (tail_ - head_) & (Capacity - 1);
    }
    
    // Empty
    bool empty() const {
        return head_ == tail_;
    }
    
    // Full
    bool full() const {
        return ((tail_ + 1) & (Capacity - 1)) == head_;
    }
    
    // Clear
    void clear() {
        head_ = 0;
        tail_ = 0;
    }
    
private:
    alignas(CACHE_LINE_SIZE) std::array<T, Capacity> buffer_;
    alignas(CACHE_LINE_SIZE) size_t head_;
    alignas(CACHE_LINE_SIZE) size_t tail_;
};

// Prefetch utilities
namespace prefetch {

// Prefetch for read (likely to be used)
inline void prefetch_for_read(const void* addr) {
#if defined(__GNUC__) || defined(__clang__)
    __builtin_prefetch(addr, 0, 3);
#else
    // Not supported
    (void)addr;
#endif
}

// Prefetch for write (likely to be written)
inline void prefetch_for_write(const void* addr) {
#if defined(__GNUC__) || defined(__clang__)
    __builtin_prefetch(addr, 1, 3);
#else
    // Not supported
    (void)addr;
#endif
}

// Prefetch with locality hint (0-3, higher = more likely to stay in cache)
inline void prefetch(const void* addr, int locality) {
#if defined(__GNUC__) || defined(__clang__)
    if (locality >= 0 && locality <= 3) {
        switch (locality) {
            case 0: __builtin_prefetch(addr, 0, 0); break;
            case 1: __builtin_prefetch(addr, 0, 1); break;
            case 2: __builtin_prefetch(addr, 0, 2); break;
            case 3: __builtin_prefetch(addr, 0, 3); break;
        }
    }
#else
    // Not supported
    (void)addr;
    (void)locality;
#endif
}

} // namespace prefetch

} // namespace core
} // namespace best_server

#endif // BEST_SERVER_CORE_CACHE_OPTIMIZATION_HPP