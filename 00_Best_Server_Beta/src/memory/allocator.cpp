// Allocator - NUMA-aware lock-free memory allocator implementation
// Optimized for better performance than Seastar

#include "best_server/memory/allocator.hpp"
#include <sys/mman.h>
#include <cstring>
#include <stdexcept>

#if BEST_SERVER_PLATFORM_LINUX
    #include <numa.h>
#endif

namespace best_server {
namespace memory {

// Thread-local cache
thread_local ThreadLocalCache Allocator::thread_cache_;

// Helper functions
inline MemoryBlockHeader* Allocator::get_header(void* ptr) const {
    if (!ptr) return nullptr;
    return reinterpret_cast<MemoryBlockHeader*>(static_cast<char*>(ptr) - sizeof(MemoryBlockHeader));
}

inline void* Allocator::get_user_ptr(MemoryBlockHeader* header) const {
    return static_cast<void*>(reinterpret_cast<char*>(header) + sizeof(MemoryBlockHeader));
}

// Allocator implementation
Allocator::Allocator() 
    : numa_enabled_(false),
      num_numa_nodes_(1) {
    
#if BEST_SERVER_PLATFORM_LINUX && defined(NUMA_VERSION)
    if (numa_available() != -1) {
        numa_enabled_.store(true, std::memory_order_relaxed);
        int max_node = numa_max_node();
        num_numa_nodes_ = max_node + 1;
        
        for (int i = 0; i <= max_node; ++i) {
            numa_nodes_.push_back(i);
        }
    }
#endif
}

Allocator::~Allocator() {
    // Note: Thread-local caches will be destroyed automatically
    // No cleanup needed for lock-free design
}

ThreadLocalCache* Allocator::get_thread_cache() {
    return &thread_cache_;
}

void* Allocator::allocate(size_t size, AllocFlags flags, int numa_node) {
    if (size == 0) return nullptr;
    
    // Determine actual NUMA node
    if (numa_node < 0 && numa_enabled_.load(std::memory_order_relaxed)) {
        numa_node = get_default_numa_node();
    }
    
    // Check if we should use huge pages
    bool use_huge_page = (flags & AllocFlags::HugePage) != AllocFlags::None && size >= HUGE_PAGE_SIZE;
    
    if (use_huge_page) {
        return allocate_huge_page(size, numa_node);
    }
    
    // Small allocations use fast path
    if (size <= MAX_SMALL_ALLOC) {
        return allocate_small(size, flags, numa_node);
    }
    
    // Large allocations
    return allocate_large(size, flags, numa_node);
}

void* Allocator::allocate_small(size_t size, AllocFlags flags, int numa_node) {
    // Try thread-local cache first (lock-free)
    void* ptr = nullptr;
    if (thread_cache_.try_allocate(size, &ptr)) {
        stats_.cache_hits.fetch_add(1, std::memory_order_relaxed);
        return ptr;
    }
    
    stats_.cache_misses.fetch_add(1, std::memory_order_relaxed);
    
    // Cache miss - allocate new block
    size_t total_size = size + sizeof(MemoryBlockHeader);
    bool use_numa = numa_enabled_.load(std::memory_order_relaxed) && (flags & AllocFlags::NUMAAware) != AllocFlags::None;
    
    void* raw_ptr = nullptr;
    if (use_numa && numa_node >= 0) {
        raw_ptr = allocate_numa(total_size, numa_node);
    } else {
        raw_ptr = ::malloc(total_size);
    }
    
    if (!raw_ptr) return nullptr;
    
    // Initialize header
    auto* header = new (raw_ptr) MemoryBlockHeader();
    header->size = size;
    header->numa_node = numa_node;
    header->flags = static_cast<uint8_t>(flags);
    header->ref_count.store(1, std::memory_order_relaxed);
    header->magic = MemoryBlockHeader::MAGIC;
    
    // Update stats
    stats_.total_allocated.fetch_add(size, std::memory_order_relaxed);
    stats_.allocation_count.fetch_add(1, std::memory_order_relaxed);
    
    uint64_t old_usage = stats_.current_usage.fetch_add(size, std::memory_order_relaxed);
    uint64_t new_usage = old_usage + size;
    
    // Try to update peak usage
    uint64_t current_peak = stats_.peak_usage.load(std::memory_order_relaxed);
    while (new_usage > current_peak) {
        if (stats_.peak_usage.compare_exchange_weak(current_peak, new_usage,
                                                    std::memory_order_relaxed)) {
            break;
        }
    }
    
    if (use_numa) {
        stats_.numa_allocations.fetch_add(1, std::memory_order_relaxed);
    }
    
    return get_user_ptr(header);
}

void* Allocator::allocate_large(size_t size, AllocFlags flags, int numa_node) {
    size_t total_size = size + sizeof(MemoryBlockHeader);
    bool use_numa = numa_enabled_.load(std::memory_order_relaxed) && (flags & AllocFlags::NUMAAware) != AllocFlags::None;
    
    void* raw_ptr = nullptr;
    if (use_numa && numa_node >= 0) {
        raw_ptr = allocate_numa(total_size, numa_node);
    } else {
        raw_ptr = ::malloc(total_size);
    }
    
    if (!raw_ptr) return nullptr;
    
    // Initialize header
    auto* header = new (raw_ptr) MemoryBlockHeader();
    header->size = size;
    header->numa_node = numa_node;
    header->flags = static_cast<uint8_t>(flags);
    header->ref_count.store(1, std::memory_order_relaxed);
    header->magic = MemoryBlockHeader::MAGIC;
    
    // Update stats
    stats_.total_allocated.fetch_add(size, std::memory_order_relaxed);
    stats_.allocation_count.fetch_add(1, std::memory_order_relaxed);
    
    uint64_t old_usage = stats_.current_usage.fetch_add(size, std::memory_order_relaxed);
    uint64_t new_usage = old_usage + size;
    
    // Try to update peak usage
    uint64_t current_peak = stats_.peak_usage.load(std::memory_order_relaxed);
    while (new_usage > current_peak) {
        if (stats_.peak_usage.compare_exchange_weak(current_peak, new_usage,
                                                    std::memory_order_relaxed)) {
            break;
        }
    }
    
    if (use_numa) {
        stats_.numa_allocations.fetch_add(1, std::memory_order_relaxed);
    }
    
    return get_user_ptr(header);
}

void* Allocator::allocate_aligned(size_t size, size_t alignment, AllocFlags flags, int numa_node) {
    if (alignment <= CACHE_LINE_SIZE) {
        return allocate(size, flags, numa_node);
    }
    
    // For larger alignments, use platform-specific functions
    void* ptr = nullptr;
    size_t total_size = size + sizeof(MemoryBlockHeader) + alignment;
    
#if BEST_SERVER_PLATFORM_LINUX
    if (posix_memalign(&ptr, alignment, total_size) != 0) {
        return nullptr;
    }
#else
    // Use malloc with manual alignment for non-Linux platforms
    ptr = malloc(total_size + alignment);
    if (!ptr) {
        return nullptr;
    }
    // Align pointer
    uintptr_t aligned_ptr = reinterpret_cast<uintptr_t>(ptr);
    aligned_ptr = (aligned_ptr + alignment - 1) & ~(alignment - 1);
    ptr = reinterpret_cast<void*>(aligned_ptr);
#endif
    
    // Initialize header
    auto* header = new (ptr) MemoryBlockHeader();
    header->size = size;
    header->numa_node = numa_node;
    header->flags = static_cast<uint8_t>(flags) | static_cast<uint8_t>(AllocFlags::CacheAligned);
    header->ref_count.store(1, std::memory_order_relaxed);
    header->magic = MemoryBlockHeader::MAGIC;
    
    // Update stats
    stats_.total_allocated.fetch_add(size, std::memory_order_relaxed);
    stats_.allocation_count.fetch_add(1, std::memory_order_relaxed);
    stats_.current_usage.fetch_add(size, std::memory_order_relaxed);
    
    return get_user_ptr(header);
}

void Allocator::free(void* ptr) {
    if (!ptr) return;
    
    auto* header = get_header(ptr);
    if (!header || header->magic != MemoryBlockHeader::MAGIC) {
        return; // Invalid pointer
    }
    
    // Decrease ref count
    uint32_t old_count = header->ref_count.fetch_sub(1, std::memory_order_acq_rel);
    if (old_count > 1) {
        return; // Still referenced
    }
    
    size_t size = header->size;
    
    // Update stats
    stats_.current_usage.fetch_sub(size, std::memory_order_relaxed);
    stats_.total_freed.fetch_add(size, std::memory_order_relaxed);
    stats_.free_count.fetch_add(1, std::memory_order_relaxed);
    
    // Try to return to thread-local cache for small allocations
    if (size <= MAX_SMALL_ALLOC) {
        thread_cache_.deallocate(ptr, size);
        return;
    }
    
    // Large allocations - free directly
    free_large(ptr, size);
}

void Allocator::free_small(void* ptr, size_t size) {
    // Note: This is called when thread cache is full
    // For now, just free directly
    auto* header = get_header(ptr);
    if (!header) return;
    
    size_t total_size = size + sizeof(MemoryBlockHeader);
    (void)total_size;  // Suppress unused variable warning
    
#if BEST_SERVER_PLATFORM_LINUX && defined(NUMA_VERSION)
    if (numa_enabled_.load(std::memory_order_relaxed) && header->numa_node >= 0) {
        numa_free(header, total_size);
    } else {
        ::free(header);
    }
#else
    ::free(header);
#endif
}

void Allocator::free_large(void* ptr, size_t size) {
    auto* header = get_header(ptr);
    if (!header) return;
    
    size_t total_size = size + sizeof(MemoryBlockHeader);
    (void)total_size;  // Suppress unused variable warning
    
#if BEST_SERVER_PLATFORM_LINUX && defined(NUMA_VERSION)
    if (numa_enabled_.load(std::memory_order_relaxed) && header->numa_node >= 0) {
        numa_free(header, total_size);
    } else {
        ::free(header);
    }
#else
    ::free(header);
#endif
}

void* Allocator::allocate_huge_page(size_t size, int numa_node) {
#if BEST_SERVER_PLATFORM_LINUX
    // Align to huge page size
    size_t aligned_size = ((size + HUGE_PAGE_SIZE - 1) / HUGE_PAGE_SIZE) * HUGE_PAGE_SIZE;
    size_t total_size = aligned_size + sizeof(MemoryBlockHeader);
    
    void* ptr = mmap(nullptr, total_size, PROT_READ | PROT_WRITE,
                     MAP_PRIVATE | MAP_ANONYMOUS | MAP_HUGETLB, -1, 0);
    
    if (ptr == MAP_FAILED) {
        return nullptr;
    }
    
    // Bind to NUMA node if specified
#if defined(NUMA_VERSION)
    if (numa_enabled_.load(std::memory_order_relaxed) && numa_node >= 0) {
        numa_tonode_memory(ptr, total_size, numa_node);
    }
#endif
    
    // Initialize header
    auto* header = new (ptr) MemoryBlockHeader();
    header->size = aligned_size;
    header->numa_node = numa_node;
    header->flags = static_cast<uint8_t>(AllocFlags::HugePage);
    header->ref_count.store(1, std::memory_order_relaxed);
    header->magic = MemoryBlockHeader::MAGIC;
    
    // Update stats
    stats_.total_allocated.fetch_add(aligned_size, std::memory_order_relaxed);
    stats_.allocation_count.fetch_add(1, std::memory_order_relaxed);
    stats_.huge_page_count.fetch_add(1, std::memory_order_relaxed);
    
    uint64_t old_usage = stats_.current_usage.fetch_add(aligned_size, std::memory_order_relaxed);
    uint64_t new_usage = old_usage + aligned_size;
    
    // Try to update peak usage
    uint64_t current_peak = stats_.peak_usage.load(std::memory_order_relaxed);
    while (new_usage > current_peak) {
        if (stats_.peak_usage.compare_exchange_weak(current_peak, new_usage,
                                                    std::memory_order_relaxed)) {
            break;
        }
    }
    
    return get_user_ptr(header);
#else
    return allocate(size, AllocFlags::None, numa_node);
#endif
}

void* Allocator::deallocate_numa(void* ptr, size_t size, int numa_node) {
    (void)ptr;    // Suppress unused parameter warning
    (void)size;   // Suppress unused parameter warning
    (void)numa_node;  // Suppress unused parameter warning
    return nullptr;
}

void* Allocator::allocate_numa(size_t size, int numa_node) {
    (void)numa_node;  // Suppress unused parameter warning
#if BEST_SERVER_PLATFORM_LINUX && defined(NUMA_VERSION)
    if (numa_enabled_.load(std::memory_order_relaxed) && 
        numa_node >= 0 && numa_node < num_numa_nodes_) {
        return numa_alloc_onnode(size, numa_node);
    }
#endif
    return ::malloc(size);
}

bool Allocator::get_block_info(void* ptr, size_t* out_size, int* out_numa_node, uint8_t* out_flags) const {
    auto* header = get_header(ptr);
    if (!header || header->magic != MemoryBlockHeader::MAGIC) {
        return false;
    }
    
    if (out_size) *out_size = header->size;
    if (out_numa_node) *out_numa_node = header->numa_node;
    if (out_flags) *out_flags = header->flags;
    
    return true;
}

AllocatorStats Allocator::stats() const {
    AllocatorStats result;
    result.total_allocated = stats_.total_allocated.load(std::memory_order_relaxed);
    result.total_freed = stats_.total_freed.load(std::memory_order_relaxed);
    result.current_usage = stats_.current_usage.load(std::memory_order_relaxed);
    result.peak_usage = stats_.peak_usage.load(std::memory_order_relaxed);
    result.allocation_count = stats_.allocation_count.load(std::memory_order_relaxed);
    result.free_count = stats_.free_count.load(std::memory_order_relaxed);
    result.huge_page_count = stats_.huge_page_count.load(std::memory_order_relaxed);
    result.numa_allocations = stats_.numa_allocations.load(std::memory_order_relaxed);
    result.cache_hits = stats_.cache_hits.load(std::memory_order_relaxed);
    result.cache_misses = stats_.cache_misses.load(std::memory_order_relaxed);
    return result;
}

void Allocator::set_numa_aware(bool enabled) {
    bool can_enable = enabled && num_numa_nodes_ > 1;
    numa_enabled_.store(can_enable, std::memory_order_release);
}

int Allocator::get_default_numa_node() const {
#if BEST_SERVER_PLATFORM_LINUX && defined(NUMA_VERSION)
    if (numa_enabled_.load(std::memory_order_relaxed)) {
        return numa_preferred();
    }
#endif
    return 0;
}

bool Allocator::pin_memory(void* ptr, size_t size) {
#if BEST_SERVER_PLATFORM_LINUX
    return mlock(ptr, size) == 0;
#else
    (void)ptr;
    (void)size;
    return false;
#endif
}

bool Allocator::unpin_memory(void* ptr) {
#if BEST_SERVER_PLATFORM_LINUX
    return munlock(ptr, 0) == 0;
#else
    (void)ptr;
    return false;
#endif
}

} // namespace memory
} // namespace best_server