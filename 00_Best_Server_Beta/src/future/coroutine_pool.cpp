// Coroutine Pool Implementation

#include "best_server/future/coroutine_pool.hpp"
#include <memory>
#include <new>
#include <mutex>

namespace best_server {
namespace future {

// CoroutineAllocator static members
CoroutineAllocator::PoolEntry* CoroutineAllocator::free_list_ = nullptr;
std::atomic<size_t> CoroutineAllocator::pool_size_{0};
std::atomic<size_t> CoroutineAllocator::max_pool_size_{DEFAULT_MAX_POOL_SIZE};
std::atomic<bool> CoroutineAllocator::pooling_enabled_{true};

// Statistics
std::atomic<size_t> CoroutineAllocator::total_allocations_{0};
std::atomic<size_t> CoroutineAllocator::total_deallocations_{0};
std::atomic<size_t> CoroutineAllocator::total_memory_used_{0};
std::atomic<size_t> CoroutineAllocator::peak_memory_used_{0};
std::atomic<size_t> CoroutineAllocator::active_coroutines_{0};

void* CoroutineAllocator::allocate(size_t size) {
    // Align size
    size = (size + FRAME_ALIGNMENT - 1) & ~(FRAME_ALIGNMENT - 1);
    
    // Update statistics
    total_allocations_.fetch_add(1, std::memory_order_relaxed);
    active_coroutines_.fetch_add(1, std::memory_order_relaxed);
    
    // Try to reuse from pool
    if (pooling_enabled_.load(std::memory_order_relaxed)) {
        PoolEntry* entry = nullptr;
        
        // Find a matching size entry
        {
            static std::mutex pool_mutex;
            std::lock_guard<std::mutex> lock(pool_mutex);
            
            PoolEntry** prev = &free_list_;
            while (*prev) {
                if ((*prev)->size >= size) {
                    entry = *prev;
                    *prev = entry->next;
                    pool_size_.fetch_sub(1, std::memory_order_relaxed);
                    break;
                }
                prev = &(*prev)->next;
            }
        }
        
        if (entry) {
            return entry->frame;
        }
    }
    
    // Allocate new frame
    void* ptr = ::operator new(size);
    
    // Update memory statistics
    size_t current_memory = total_memory_used_.fetch_add(size, std::memory_order_relaxed) + size;
    size_t peak = peak_memory_used_.load(std::memory_order_relaxed);
    while (current_memory > peak && 
           !peak_memory_used_.compare_exchange_weak(peak, current_memory,
                                                    std::memory_order_relaxed)) {
        // Retry
    }
    
    return ptr;
}

void CoroutineAllocator::deallocate(void* ptr, size_t size) {
    if (!ptr) return;
    
    // Align size
    size = (size + FRAME_ALIGNMENT - 1) & ~(FRAME_ALIGNMENT - 1);
    
    // Update statistics
    total_deallocations_.fetch_add(1, std::memory_order_relaxed);
    active_coroutines_.fetch_sub(1, std::memory_order_relaxed);
    total_memory_used_.fetch_sub(size, std::memory_order_relaxed);
    
    // Try to return to pool
    if (pooling_enabled_.load(std::memory_order_relaxed)) {
        size_t current_pool_size = pool_size_.load(std::memory_order_relaxed);
        size_t max_size = max_pool_size_.load(std::memory_order_relaxed);
        
        if (current_pool_size < max_size) {
            // Create new pool entry
            PoolEntry* entry = new PoolEntry;
            entry->frame = ptr;
            entry->size = size;
            
            // Add to free list
            {
                static std::mutex pool_mutex;
                std::lock_guard<std::mutex> lock(pool_mutex);
                
                entry->next = free_list_;
                free_list_ = entry;
                pool_size_.fetch_add(1, std::memory_order_relaxed);
            }
            
            return;
        }
    }
    
    // Deallocate
    ::operator delete(ptr);
}

CoroutinePoolStats CoroutineAllocator::get_stats() {
    CoroutinePoolStats stats;
    stats.total_coroutines = total_allocations_.load(std::memory_order_relaxed);
    stats.active_coroutines = active_coroutines_.load(std::memory_order_relaxed);
    stats.idle_coroutines = pool_size_.load(std::memory_order_relaxed);
    stats.total_memory_used = total_memory_used_.load(std::memory_order_relaxed);
    stats.peak_memory_used = peak_memory_used_.load(std::memory_order_relaxed);
    stats.allocations = total_allocations_.load(std::memory_order_relaxed);
    stats.deallocations = total_deallocations_.load(std::memory_order_relaxed);
    return stats;
}

void CoroutineAllocator::reset_stats() {
    total_allocations_ = 0;
    total_deallocations_ = 0;
    total_memory_used_ = 0;
    peak_memory_used_ = 0;
    active_coroutines_ = 0;
}

void CoroutineAllocator::set_max_pool_size(size_t max_size) {
    max_pool_size_.store(max_size, std::memory_order_relaxed);
}

size_t CoroutineAllocator::get_max_pool_size() {
    return max_pool_size_.load(std::memory_order_relaxed);
}

void CoroutineAllocator::enable_pooling(bool enable) {
    pooling_enabled_.store(enable, std::memory_order_relaxed);
}

bool CoroutineAllocator::is_pooling_enabled() {
    return pooling_enabled_.load(std::memory_order_relaxed);
}

// Stack size optimization
static std::atomic<bool> coroutine_optimization_enabled_{true};
static std::atomic<size_t> coroutine_stack_size_{CoroutineStackSize::Default};

bool set_coroutine_stack_size(size_t size) {
    // Stack size is managed by the compiler/runtime
    // This is mainly for documentation and future use
    coroutine_stack_size_.store(size, std::memory_order_relaxed);
    return true;
}

size_t get_coroutine_stack_size() {
    return coroutine_stack_size_.load(std::memory_order_relaxed);
}

void optimize_coroutine_allocation(bool enable) {
    coroutine_optimization_enabled_.store(enable, std::memory_order_relaxed);
}

bool is_coroutine_optimization_enabled() {
    return coroutine_optimization_enabled_.load(std::memory_order_relaxed);
}

} // namespace future
} // namespace best_server