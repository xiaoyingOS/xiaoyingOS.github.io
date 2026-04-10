// Allocator - NUMA-aware lock-free memory allocator with zero-copy support
// Optimized for better performance than Seastar
// 
// Provides optimized memory allocation with:
// - NUMA-aware allocation with per-CPU caches
// - Huge page support
// - Lock-free design for small allocations
// - Memory pooling with thread-local caches
// - Zero-copy buffer management
// - Cache-line alignment

#ifndef BEST_SERVER_MEMORY_ALLOCATOR_HPP
#define BEST_SERVER_MEMORY_ALLOCATOR_HPP

#include <memory>
#include <cstddef>
#include <cstdint>
#include <atomic>
#include <vector>
#include <array>
#include <thread>

#if BEST_SERVER_PLATFORM_LINUX && !defined(__ANDROID__)
    #include <numa.h>
#endif

namespace best_server {
namespace memory {

// Allocation flags
enum class AllocFlags : uint32_t {
    None = 0,
    ZeroCopy = 0x01,
    HugePage = 0x02,
    NUMAAware = 0x04,
    CacheAligned = 0x08,
    PageAligned = 0x10
};

inline AllocFlags operator|(AllocFlags a, AllocFlags b) {
    return static_cast<AllocFlags>(static_cast<uint32_t>(a) | static_cast<uint32_t>(b));
}

inline AllocFlags operator&(AllocFlags a, AllocFlags b) {
    return static_cast<AllocFlags>(static_cast<uint32_t>(a) & static_cast<uint32_t>(b));
}

// Memory block metadata (stored in header of allocated block)
struct alignas(16) MemoryBlockHeader {
    size_t size;
    int numa_node;
    uint8_t flags;
    std::atomic<uint32_t> ref_count;
    
    // Magic number for validation
    static constexpr uint64_t MAGIC = 0x4C4F434145524241ULL; // "ALLOCMEM"
    uint64_t magic;
};

// Allocator statistics (lock-free)
struct AllocatorStats {
    uint64_t total_allocated{0};
    uint64_t total_freed{0};
    uint64_t current_usage{0};
    uint64_t peak_usage{0};
    uint32_t allocation_count{0};
    uint32_t free_count{0};
    uint32_t huge_page_count{0};
    uint32_t numa_allocations{0};
    uint32_t cache_hits{0};
    uint32_t cache_misses{0};
};

// Per-thread allocation cache for fast allocations
class alignas(64) ThreadLocalCache {
public:
    static constexpr size_t MAX_CACHE_SIZE = 32;
    static constexpr size_t CACHE_BUCKETS = 8;
    
    struct CacheEntry {
        void* ptr;
        size_t size;
    };
    
    ThreadLocalCache() {}
    
    bool try_allocate(size_t size, void** out_ptr) {
        size_t bucket = size_to_bucket(size);
        auto& cache = caches_[bucket];
        
        if (cache.count > 0) {
            --cache.count;
            *out_ptr = cache.entries[cache.count].ptr;
            return true;
        }
        return false;
    }
    
    void deallocate(void* ptr, size_t size) {
        size_t bucket = size_to_bucket(size);
        auto& cache = caches_[bucket];
        
        if (cache.count < MAX_CACHE_SIZE) {
            cache.entries[cache.count].ptr = ptr;
            cache.entries[cache.count].size = size;
            ++cache.count;
        }
        // If cache is full, the caller should free globally
    }
    
    void flush() {
        for (auto& cache : caches_) {
            cache.count = 0;
        }
    }
    
private:
    static constexpr size_t size_to_bucket(size_t size) {
        if (size <= 64) return 0;
        if (size <= 128) return 1;
        if (size <= 256) return 2;
        if (size <= 512) return 3;
        if (size <= 1024) return 4;
        if (size <= 2048) return 5;
        if (size <= 4096) return 6;
        return 7;
    }
    
    struct CacheBucket {
        CacheEntry entries[MAX_CACHE_SIZE];
        uint32_t count;
    };
    
    std::array<CacheBucket, CACHE_BUCKETS> caches_;
};

// NUMA-aware lock-free allocator
class Allocator {
public:
    static constexpr size_t CACHE_LINE_SIZE = 64;
    static constexpr size_t PAGE_SIZE = 4096;
    static constexpr size_t HUGE_PAGE_SIZE = 2 * 1024 * 1024; // 2MB
    static constexpr size_t MAX_SMALL_ALLOC = 4096;
    
    Allocator();
    ~Allocator();
    
    // Allocate memory
    void* allocate(size_t size, AllocFlags flags = AllocFlags::None, int numa_node = -1);
    
    // Free memory
    void free(void* ptr);
    
    // Allocate aligned memory
    void* allocate_aligned(size_t size, size_t alignment, AllocFlags flags = AllocFlags::None, int numa_node = -1);
    
    // Get memory block info
    bool get_block_info(void* ptr, size_t* out_size, int* out_numa_node, uint8_t* out_flags) const;
    
    // Get statistics
    AllocatorStats stats() const;
    
    // Enable/disable NUMA awareness
    void set_numa_aware(bool enabled);
    
    // Get default NUMA node for current thread
    int get_default_numa_node() const;
    
    // Pin memory (for DMA)
    bool pin_memory(void* ptr, size_t size);
    
    // Unpin memory
    bool unpin_memory(void* ptr);
    
    // Get thread-local cache
    ThreadLocalCache* get_thread_cache();
    
private:
    void* allocate_small(size_t size, AllocFlags flags, int numa_node);
    void* allocate_large(size_t size, AllocFlags flags, int numa_node);
    void* allocate_huge_page(size_t size, int numa_node);
    void* allocate_numa(size_t size, int numa_node);
    void* deallocate_numa(void* ptr, size_t size, int numa_node);
    
    void free_small(void* ptr, size_t size);
    void free_large(void* ptr, size_t size);
    
    MemoryBlockHeader* get_header(void* ptr) const;
    void* get_user_ptr(MemoryBlockHeader* header) const;
    
    std::atomic<bool> numa_enabled_;
    std::vector<int> numa_nodes_;
    int num_numa_nodes_;
    
    // Internal statistics (atomic)
    struct AtomicAllocatorStats {
        std::atomic<uint64_t> total_allocated{0};
        std::atomic<uint64_t> total_freed{0};
        std::atomic<uint64_t> current_usage{0};
        std::atomic<uint64_t> peak_usage{0};
        std::atomic<uint32_t> allocation_count{0};
        std::atomic<uint32_t> free_count{0};
        std::atomic<uint32_t> huge_page_count{0};
        std::atomic<uint32_t> numa_allocations{0};
        std::atomic<uint32_t> cache_hits{0};
        std::atomic<uint32_t> cache_misses{0};
    };
    
    AtomicAllocatorStats stats_;
    
    // Thread-local cache management
    static thread_local ThreadLocalCache thread_cache_;
    
    // Huge page pool (simplified)
    std::atomic<void*> huge_page_pool_{nullptr};
};

// Smart pointer with allocator integration
template<typename T>
class AllocatedPtr {
public:
    AllocatedPtr() : ptr_(nullptr), allocator_(nullptr) {}
    
    template<typename... Args>
    explicit AllocatedPtr(Allocator* allocator, Args&&... args)
        : allocator_(allocator) {
        void* mem = allocator_->allocate(sizeof(T), AllocFlags::CacheAligned);
        ptr_ = new (mem) T(std::forward<Args>(args)...);
    }
    
    ~AllocatedPtr() {
        reset();
    }
    
    void reset() {
        if (ptr_) {
            ptr_->~T();
            allocator_->free(ptr_);
            ptr_ = nullptr;
        }
    }
    
    T* get() const { return ptr_; }
    T* operator->() const { return ptr_; }
    T& operator*() const { return *ptr_; }
    explicit operator bool() const { return ptr_ != nullptr; }
    
    // Disable copy
    AllocatedPtr(const AllocatedPtr&) = delete;
    AllocatedPtr& operator=(const AllocatedPtr&) = delete;
    
    // Enable move
    AllocatedPtr(AllocatedPtr&& other) noexcept
        : ptr_(other.ptr_), allocator_(other.allocator_) {
        other.ptr_ = nullptr;
    }
    
    AllocatedPtr& operator=(AllocatedPtr&& other) noexcept {
        if (this != &other) {
            reset();
            ptr_ = other.ptr_;
            allocator_ = other.allocator_;
            other.ptr_ = nullptr;
        }
        return *this;
    }
    
private:
    T* ptr_;
    Allocator* allocator_;
};

} // namespace memory
} // namespace best_server

#endif // BEST_SERVER_MEMORY_ALLOCATOR_HPP