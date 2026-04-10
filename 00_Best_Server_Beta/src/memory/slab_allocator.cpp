// SlabAllocator implementation

#include "best_server/memory/slab_allocator.hpp"
#include <cstdlib>
#include <cstring>
#include <stdexcept>

namespace best_server {
namespace memory {

// ==================== Slab Implementation ====================

Slab::Slab(size_t object_size, size_t capacity)
    : memory_(nullptr)
    , free_list_(nullptr)
    , used_(0)
    , capacity_(capacity)
    , object_size_(object_size)
    , next_(nullptr)
{
    // Allocate aligned memory
    if (posix_memalign(&memory_, 64, object_size * capacity) != 0) {
        throw std::bad_alloc();
    }
    
    // Initialize free list
    char* ptr = static_cast<char*>(memory_);
    for (size_t i = 0; i < capacity_; ++i) {
        void** next = reinterpret_cast<void**>(ptr);
        *next = free_list_;
        free_list_ = ptr;
        ptr += object_size_;
    }
}

Slab::~Slab() {
    if (memory_) {
        free(memory_);
    }
}

void* Slab::allocate() {
    if (is_full()) {
        return nullptr;
    }
    
    void* ptr = free_list_;
    free_list_ = *reinterpret_cast<void**>(ptr);
    used_++;
    return ptr;
}

bool Slab::deallocate(void* ptr) {
    if (ptr < memory_ || ptr >= static_cast<char*>(memory_) + object_size_ * capacity_) {
        return false; // Not from this slab
    }
    
    *reinterpret_cast<void**>(ptr) = free_list_;
    free_list_ = ptr;
    used_--;
    return true;
}

// ==================== SlabCache Implementation ====================

SlabCache::SlabCache(size_t object_size)
    : object_size_(object_size)
    , slabs_(nullptr)
    , partial_slabs_(nullptr)
    , empty_slabs_(nullptr)
{
}

SlabCache::~SlabCache() {
    // Free all slabs
    Slab* slab = slabs_;
    while (slab) {
        Slab* next = slab->next_;
        delete slab;
        slab = next;
    }
}

void* SlabCache::allocate() {
    std::lock_guard<std::mutex> lock(mutex_);
    
    // Try to get from partial slab
    Slab* slab = get_partial_slab();
    if (slab) {
        void* ptr = slab->allocate();
        if (slab->is_full()) {
            // Move to full slabs (not tracked explicitly)
        }
        return ptr;
    }
    
    // Need to create new slab
    create_new_slab();
    slab = partial_slabs_;
    return slab->allocate();
}

void SlabCache::deallocate(void* ptr) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    // Find the slab containing this pointer
    Slab* slab = slabs_;
    while (slab) {
        if (slab->deallocate(ptr)) {
            // Successfully deallocated
            if (slab->is_empty()) {
                // Move to empty slabs
                if (slab != partial_slabs_) {
                    // Remove from partial list
                    // Simplified: just mark as empty
                }
            }
            return;
        }
        slab = slab->next_;
    }
}

size_t SlabCache::total_used() const {
    std::lock_guard<std::mutex> lock(mutex_);
    
    size_t total = 0;
    Slab* slab = slabs_;
    while (slab) {
        total += slab->used();
        slab = slab->next_;
    }
    return total;
}

size_t SlabCache::total_capacity() const {
    std::lock_guard<std::mutex> lock(mutex_);
    
    size_t total = 0;
    Slab* slab = slabs_;
    while (slab) {
        total += slab->capacity();
        slab = slab->next_;
    }
    return total;
}

Slab* SlabCache::get_partial_slab() {
    // Get a slab with free space
    if (partial_slabs_ && !partial_slabs_->is_full()) {
        return partial_slabs_;
    }
    return nullptr;
}

void SlabCache::create_new_slab() {
    size_t capacity = Slab::DEFAULT_SLAB_SIZE / object_size_;
    Slab* slab = new Slab(object_size_, capacity);
    
    slab->next_ = slabs_;
    slabs_ = slab;
    partial_slabs_ = slab;
}

// ==================== SlabAllocator Implementation ====================

SlabAllocator::SlabAllocator() {
    // Initialize caches for each size class
    for (size_t i = 0; i < NUM_SIZE_CLASSES; ++i) {
        caches_[i] = new SlabCache(SIZE_CLASSES[i]);
    }
}

SlabAllocator::~SlabAllocator() {
    for (size_t i = 0; i < NUM_SIZE_CLASSES; ++i) {
        delete caches_[i];
    }
}

SlabAllocator& SlabAllocator::instance() {
    static SlabAllocator instance;
    return instance;
}

void* SlabAllocator::allocate(size_t size) {
    total_allocated_.fetch_add(size, std::memory_order_relaxed);
    
    // Use thread-local cache for better performance
    thread_local ThreadLocalSlabCache local_cache;
    void* ptr = local_cache.allocate(size);
    
    if (!ptr) {
        // Cache miss, allocate from global allocator
        size_t size_class = get_size_class(size);
        ptr = caches_[size_class]->allocate();
    }
    
    return ptr;
}

void SlabAllocator::deallocate(void* ptr, size_t size) {
    total_freed_.fetch_add(size, std::memory_order_relaxed);
    
    // Use thread-local cache for better performance
    thread_local ThreadLocalSlabCache local_cache;
    local_cache.deallocate(ptr, size);
}

SlabAllocator::Stats SlabAllocator::get_stats() const {
    std::lock_guard<std::mutex> lock(stats_mutex_);
    
    Stats stats;
    stats.total_allocated = total_allocated_.load(std::memory_order_relaxed);
    stats.total_freed = total_freed_.load(std::memory_order_relaxed);
    stats.current_usage = stats.total_allocated - stats.total_freed;
    stats.peak_usage = stats.current_usage;
    
    size_t total_capacity = 0;
    size_t slab_count = 0;
    
    for (size_t i = 0; i < NUM_SIZE_CLASSES; ++i) {
        total_capacity += caches_[i]->total_capacity();
        slab_count += (total_capacity / Slab::DEFAULT_SLAB_SIZE);
    }
    
    stats.slab_count = slab_count;
    stats.fragmentation_ratio = (total_capacity > 0) ? 
        (100 * (total_capacity - stats.current_usage) / total_capacity) : 0;
    
    return stats;
}

size_t SlabAllocator::get_size_class(size_t size) const {
    // Find the smallest size class that can accommodate the request
    for (size_t i = 0; i < NUM_SIZE_CLASSES; ++i) {
        if (size <= SIZE_CLASSES[i]) {
            return i;
        }
    }
    // Too large for slab allocator
    return NUM_SIZE_CLASSES - 1;
}

// ==================== ThreadLocalSlabCache Implementation ====================

ThreadLocalSlabCache::ThreadLocalSlabCache()
    : cache_index_(0)
    , allocator_(SlabAllocator::instance())
{
    // Initialize cache entries
    for (size_t i = 0; i < CACHE_SIZE; ++i) {
        cache_[i].ptr = nullptr;
        cache_[i].size = 0;
    }
}

ThreadLocalSlabCache::~ThreadLocalSlabCache() {
    flush_cache();
}

void* ThreadLocalSlabCache::allocate(size_t size) {
    // Try to reuse from cache first
    for (size_t i = 0; i < CACHE_SIZE; ++i) {
        if (cache_[i].ptr && cache_[i].size == size) {
            void* ptr = cache_[i].ptr;
            cache_[i].ptr = nullptr;
            cache_[i].size = 0;
            return ptr;
        }
    }
    
    // Allocate from global allocator
    return allocator_.allocate(size);
}

void ThreadLocalSlabCache::deallocate(void* ptr, size_t size) {
    // Add to cache if not full
    if (cache_[cache_index_].ptr == nullptr) {
        cache_[cache_index_].ptr = ptr;
        cache_[cache_index_].size = size;
        cache_index_ = (cache_index_ + 1) % CACHE_SIZE;
    } else {
        // Cache full, flush and add
        flush_cache();
        cache_[cache_index_].ptr = ptr;
        cache_[cache_index_].size = size;
        cache_index_ = (cache_index_ + 1) % CACHE_SIZE;
    }
}

void ThreadLocalSlabCache::flush_cache() {
    for (size_t i = 0; i < CACHE_SIZE; ++i) {
        if (cache_[i].ptr) {
            allocator_.deallocate(cache_[i].ptr, cache_[i].size);
            cache_[i].ptr = nullptr;
            cache_[i].size = 0;
        }
    }
}

} // namespace memory
} // namespace best_server