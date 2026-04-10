// LockFreeQueue - High-performance lock-free MPMC queue
// 
// Implements a lock-free multiple-producer multiple-consumer queue
// based on Dmitry Vyukov's algorithm with cache-line alignment
// to avoid false sharing.
//
// Features:
// - Wait-free producers
// - Lock-free consumers
// - Cache-line aligned for performance
// - No false sharing
// - Fixed capacity (power of 2)

#ifndef BEST_SERVER_CORE_LOCKFREE_QUEUE_HPP
#define BEST_SERVER_CORE_LOCKFREE_QUEUE_HPP

#include <atomic>
#include <memory>
#include <type_traits>

namespace best_server {
namespace core {

// Lock-free MPMC queue
template<typename T, size_t Capacity = 1024>
class LockFreeQueue {
    static_assert((Capacity & (Capacity - 1)) == 0, 
                  "Capacity must be a power of 2");
    static_assert(std::is_move_constructible<T>::value,
                  "T must be move constructible");
    
private:
    static constexpr size_t CACHE_LINE_SIZE = 64;
    
    // Node in the ring buffer
    struct Node {
        std::atomic<T*> data;
        std::atomic<size_t> sequence;
        
        Node() : data(nullptr), sequence(0) {}
    };
    
    // Ring buffer
    Node* buffer_;
    const size_t capacity_;
    alignas(CACHE_LINE_SIZE) std::atomic<size_t> write_pos_;
    alignas(CACHE_LINE_SIZE) std::atomic<size_t> read_pos_;
    
public:
    explicit LockFreeQueue(size_t capacity = Capacity)
        : buffer_(new Node[capacity])
        , capacity_(capacity)
        , write_pos_(0)
        , read_pos_(0)
    {
        // Initialize sequence numbers
        for (size_t i = 0; i < capacity_; ++i) {
            buffer_[i].sequence.store(i, std::memory_order_relaxed);
        }
    }
    
    ~LockFreeQueue() {
        delete[] buffer_;
    }
    
    // Disable copy and move
    LockFreeQueue(const LockFreeQueue&) = delete;
    LockFreeQueue& operator=(const LockFreeQueue&) = delete;
    LockFreeQueue(LockFreeQueue&&) = delete;
    LockFreeQueue& operator=(LockFreeQueue&&) = delete;
    
    // Try to push an element (wait-free for producers)
    bool try_push(T&& item) {
        size_t pos = write_pos_.load(std::memory_order_relaxed);
        Node* node = &buffer_[pos & (capacity_ - 1)];
        
        size_t seq = node->sequence.load(std::memory_order_acquire);
        intptr_t dif = (intptr_t)seq - (intptr_t)pos;
        
        // If difference is not 0, queue is full
        if (dif != 0) {
            return false;
        }
        
        // Reserve the slot
        if (!write_pos_.compare_exchange_weak(
                pos, pos + 1, 
                std::memory_order_relaxed,
                std::memory_order_relaxed)) {
            return false;
        }
        
        // Write the data
        node->data.store(new T(std::move(item)), std::memory_order_relaxed);
        
        // Publish the data
        node->sequence.store(pos + 1, std::memory_order_release);
        
        return true;
    }
    
    // Try to pop an element (lock-free for consumers)
    bool try_pop(T& item) {
        size_t pos = read_pos_.load(std::memory_order_relaxed);
        Node* node = &buffer_[pos & (capacity_ - 1)];
        
        size_t seq = node->sequence.load(std::memory_order_acquire);
        intptr_t dif = (intptr_t)seq - (intptr_t)(pos + 1);
        
        // If difference is not 0, queue is empty
        if (dif != 0) {
            return false;
        }
        
        // Reserve the slot
        if (!read_pos_.compare_exchange_weak(
                pos, pos + 1,
                std::memory_order_relaxed,
                std::memory_order_relaxed)) {
            return false;
        }
        
        // Read the data
        T* data = node->data.load(std::memory_order_relaxed);
        item = std::move(*data);
        delete data;
        node->data.store(nullptr, std::memory_order_relaxed);
        
        // Publish the empty slot
        node->sequence.store(pos + capacity_, std::memory_order_release);
        
        return true;
    }
    
    // Push an element with retry
    void push(T&& item) {
        while (!try_push(std::move(item))) {
            // Spin or use exponential backoff
            #if defined(__x86_64__)
            __builtin_ia32_pause();
            #elif defined(__aarch64__)
            __asm__ volatile("yield" ::: "memory");
            #endif
        }
    }
    
    // Pop an element with retry
    bool pop(T& item) {
        while (!try_pop(item)) {
            #if defined(__x86_64__)
            __builtin_ia32_pause();
            #elif defined(__aarch64__)
            __asm__ volatile("yield" ::: "memory");
            #endif
            return false;
        }
        return true;
    }
    
    // Get approximate size (not thread-safe)
    size_t size() const {
        size_t write = write_pos_.load(std::memory_order_relaxed);
        size_t read = read_pos_.load(std::memory_order_relaxed);
        return write - read;
    }
    
    // Check if queue is empty (approximate)
    bool empty() const {
        size_t write = write_pos_.load(std::memory_order_relaxed);
        size_t read = read_pos_.load(std::memory_order_relaxed);
        return write == read;
    }
    
    // Get capacity
    size_t capacity() const {
        return capacity_;
    }
};

// SPSC queue (Single Producer Single Consumer)
// More optimized than MPMC for single-threaded scenarios
template<typename T, size_t Capacity = 1024>
class SpscQueue {
    static_assert((Capacity & (Capacity - 1)) == 0, 
                  "Capacity must be a power of 2");
    
private:
    static constexpr size_t CACHE_LINE_SIZE = 64;
    
    alignas(CACHE_LINE_SIZE) std::atomic<size_t> write_pos_;
    alignas(CACHE_LINE_SIZE) std::atomic<size_t> read_pos_;
    
    std::unique_ptr<T[]> buffer_;
    const size_t capacity_;
    
public:
    explicit SpscQueue(size_t capacity = Capacity)
        : write_pos_(0)
        , read_pos_(0)
        , buffer_(std::make_unique<T[]>(capacity))
        , capacity_(capacity)
    {}
    
    // Try to push (wait-free)
    bool try_push(T&& item) {
        const size_t pos = write_pos_.load(std::memory_order_relaxed);
        const size_t next_pos = (pos + 1);
        
        // Check if queue is full
        if (next_pos - read_pos_.load(std::memory_order_acquire) == capacity_) {
            return false;
        }
        
        buffer_[pos & (capacity_ - 1)] = std::move(item);
        write_pos_.store(next_pos, std::memory_order_release);
        
        return true;
    }
    
    // Try to pop (wait-free)
    bool try_pop(T& item) {
        const size_t pos = read_pos_.load(std::memory_order_relaxed);
        
        // Check if queue is empty
        if (pos == write_pos_.load(std::memory_order_acquire)) {
            return false;
        }
        
        item = std::move(buffer_[pos & (capacity_ - 1)]);
        read_pos_.store(pos + 1, std::memory_order_release);
        
        return true;
    }
    
    size_t size() const {
        return write_pos_.load(std::memory_order_relaxed) - 
               read_pos_.load(std::memory_order_relaxed);
    }
    
    bool empty() const {
        return size() == 0;
    }
    
    size_t capacity() const {
        return capacity_;
    }
};

} // namespace core
} // namespace best_server

#endif // BEST_SERVER_CORE_LOCKFREE_QUEUE_HPP