// SlabAllocator - High-performance slab memory allocator
// 
// Implements a slab-based memory allocator similar to FreeBSD's
// slab allocator and Linux's SLAB allocator. Provides:
// - Fast allocation/deallocation
// - Reduced fragmentation
// - Object size categorization
// - Thread-local caching
//
// Features:
// - Size classes for common object sizes
// - Per-slab free list
// - Automatic slab management
// - Low overhead

#ifndef BEST_SERVER_MEMORY_SLAB_ALLOCATOR_HPP
#define BEST_SERVER_MEMORY_SLAB_ALLOCATOR_HPP

#include <atomic>
#include <mutex>
#include <vector>
#include <array>
#include <cstdint>
#include <cstring>
#include <new>

namespace best_server {
namespace memory {

// Size classes for slab allocator
constexpr size_t SIZE_CLASSES[] = {
    8, 16, 32, 64, 128, 256, 512, 1024, 2048, 4096, 8192, 16384
};

constexpr size_t NUM_SIZE_CLASSES = sizeof(SIZE_CLASSES) / sizeof(SIZE_CLASSES[0]);

// Slab structure
class Slab {
public:
    static constexpr size_t DEFAULT_SLAB_SIZE = 256 * 1024; // 256KB
    
    Slab(size_t object_size, size_t capacity);
    ~Slab();
    
    void* allocate();
    bool deallocate(void* ptr);
    
    bool is_full() const { return used_ == capacity_; }
    bool is_empty() const { return used_ == 0; }
    size_t used() const { return used_; }
    size_t capacity() const { return capacity_; }
    Slab* next() const { return next_; }
    
    friend class SlabCache;
    
private:
    void* memory_;          // Start of slab memory
    void* free_list_;       // Free list head
    size_t used_;           // Number of used objects
    size_t capacity_;       // Total capacity
    size_t object_size_;    // Size of each object
    Slab* next_;            // Next slab in list
};

// Slab cache for a specific size class
class SlabCache {
public:
    explicit SlabCache(size_t object_size);
    ~SlabCache();
    
    void* allocate();
    void deallocate(void* ptr);
    
    size_t object_size() const { return object_size_; }
    size_t total_used() const;
    size_t total_capacity() const;
    
private:
    Slab* get_partial_slab();
    void create_new_slab();
    
    const size_t object_size_;
    Slab* slabs_;              // List of all slabs
    Slab* partial_slabs_;       // List of partially used slabs
    [[maybe_unused]] Slab* empty_slabs_;         // List of empty slabs
    mutable std::mutex mutex_;
};

// Global slab allocator
class SlabAllocator {
public:
    static SlabAllocator& instance();
    
    void* allocate(size_t size);
    void deallocate(void* ptr, size_t size);
    
    // Statistics
    struct Stats {
        size_t total_allocated;
        size_t total_freed;
        size_t current_usage;
        size_t peak_usage;
        size_t slab_count;
        size_t fragmentation_ratio;
    };
    
    Stats get_stats() const;
    
private:
    SlabAllocator();
    ~SlabAllocator();
    
    size_t get_size_class(size_t size) const;
    
    std::array<SlabCache*, NUM_SIZE_CLASSES> caches_;
    std::atomic<size_t> total_allocated_{0};
    std::atomic<size_t> total_freed_{0};
    mutable std::mutex stats_mutex_;
};

// Thread-local cache for slab allocator
// Reduces contention on global allocator
class ThreadLocalSlabCache {
public:
    static constexpr size_t CACHE_SIZE = 64;
    
    ThreadLocalSlabCache();
    ~ThreadLocalSlabCache();
    
    void* allocate(size_t size);
    void deallocate(void* ptr, size_t size);
    
private:
    struct CacheEntry {
        void* ptr;
        size_t size;
    };
    
    void flush_cache();
    
    CacheEntry cache_[CACHE_SIZE];
    size_t cache_index_;
    SlabAllocator& allocator_;
};

// RAII wrapper for slab allocated memory
template<typename T>
class SlabPtr {
public:
    explicit SlabPtr(T* ptr = nullptr) : ptr_(ptr) {}
    
    ~SlabPtr() {
        if (ptr_) {
            ThreadLocalSlabCache cache;
            cache.deallocate(ptr_, sizeof(T));
        }
    }
    
    // Disable copy
    SlabPtr(const SlabPtr&) = delete;
    SlabPtr& operator=(const SlabPtr&) = delete;
    
    // Enable move
    SlabPtr(SlabPtr&& other) noexcept : ptr_(other.ptr_) {
        other.ptr_ = nullptr;
    }
    
    SlabPtr& operator=(SlabPtr&& other) noexcept {
        if (this != &other) {
            if (ptr_) {
                ThreadLocalSlabCache cache;
                cache.deallocate(ptr_, sizeof(T));
            }
            ptr_ = other.ptr_;
            other.ptr_ = nullptr;
        }
        return *this;
    }
    
    T* get() const { return ptr_; }
    T& operator*() const { return *ptr_; }
    T* operator->() const { return ptr_; }
    explicit operator bool() const { return ptr_ != nullptr; }
    
    void reset(T* ptr = nullptr) {
        if (ptr_) {
            ThreadLocalSlabCache cache;
            cache.deallocate(ptr_, sizeof(T));
        }
        ptr_ = ptr;
    }
    
    T* release() {
        T* tmp = ptr_;
        ptr_ = nullptr;
        return tmp;
    }
    
private:
    T* ptr_;
};

// Convenience function to allocate with slab allocator
template<typename T, typename... Args>
SlabPtr<T> make_slab(Args&&... args) {
    ThreadLocalSlabCache cache;
    void* ptr = cache.allocate(sizeof(T));
    return SlabPtr<T>(new (ptr) T(std::forward<Args>(args)...));
}

} // namespace memory
} // namespace best_server

#endif // BEST_SERVER_MEMORY_SLAB_ALLOCATOR_HPP