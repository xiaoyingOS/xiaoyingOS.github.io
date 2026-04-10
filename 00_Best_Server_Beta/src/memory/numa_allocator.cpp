// NUMA Allocator Implementation

#include "best_server/memory/numa_allocator.hpp"
#include <cstring>
#include <cstdlib>
#include <stdexcept>

// Check if NUMA is available at compile time
#if defined(__linux__) && defined(HAVE_LIBNUMA)

// If libnuma is available, include it
#include <numa.h>

namespace best_server {
namespace memory {

// NUMA implementation with libnuma
bool NumaAllocator::is_available() {
    return numa_available() >= 0;
}

int NumaAllocator::get_num_nodes() {
    if (!is_available()) return 1;
    return numa_num_configured_nodes();
}

int NumaAllocator::get_preferred_node() {
    if (!is_available()) return 0;
    return numa_preferred();
}

bool NumaAllocator::set_preferred_node(int node) {
    if (!is_available()) return false;
    numa_set_preferred(node);
    return true;
}

bool NumaAllocator::get_node_info(int node, NumaNodeInfo& info) {
    if (!is_available()) return false;
    
    info.node_id = node;
    info.total_memory = numa_node_size64(node, nullptr);
    info.free_memory = numa_node_size64(node, &info.free_memory);
    info.cpus = 0;
    
    // Get CPUs for this node
    struct bitmask* cpus = numa_allocate_cpumask();
    numa_node_to_cpus(node, cpus);
    info.cpus = numa_num_configured_cpus();
    numa_free_cpumask(cpus);
    
    info.distance = 0;
    return true;
}

void* NumaAllocator::allocate_node(size_t size, int node) {
    if (!is_available()) {
        return malloc(size);
    }
    return numa_alloc_onnode(size, node);
}

void NumaAllocator::free_node(void* ptr, size_t size, int node) {
    if (!is_available()) {
        free(ptr);
    } else {
        numa_free(ptr, size);
    }
}

void* NumaAllocator::allocate_local(size_t size) {
    if (!is_available()) {
        return malloc(size);
    }
    return numa_alloc_local(size);
}

void NumaAllocator::free_local(void* ptr, size_t size) {
    if (!is_available()) {
        free(ptr);
    } else {
        numa_free(ptr, size);
    }
}

bool NumaAllocator::set_policy(NumaPolicy policy, int preferred_node) {
    if (!is_available()) return false;
    
    switch (policy) {
        case NumaPolicy::Default:
            numa_set_localalloc();
            break;
        case NumaPolicy::Local:
            numa_set_localalloc();
            break;
        case NumaPolicy::Interleave:
            numa_set_interleave_mask(numa_all_nodes_ptr);
            break;
        case NumaPolicy::Preferred:
            if (preferred_node >= 0) {
                numa_set_preferred(preferred_node);
            }
            break;
        case NumaPolicy::Bind:
            if (preferred_node >= 0) {
                struct bitmask* mask = numa_allocate_nodemask();
                numa_bitmask_clearall(mask);
                numa_bitmask_setbit(mask, preferred_node);
                numa_bind(mask);
                numa_free_nodemask(mask);
            }
            break;
        default:
            return false;
    }
    return true;
}

NumaPolicy NumaAllocator::get_policy() {
    // Simplified implementation
    return NumaPolicy::Default;
}

bool NumaAllocator::migrate(void* ptr, size_t size, int target_node) {
    if (!is_available()) return false;
    return numa_migrate_pages(target_node, 1, ptr) == 0;
}

int NumaAllocator::get_memory_node(void* ptr) {
    if (!is_available()) return 0;
    return numa_addr_to_node(ptr);
}

void* NumaAllocator::allocate_aligned_node(size_t size, size_t alignment, int node) {
    if (!is_available()) {
        return aligned_alloc(alignment, size);
    }
    return numa_alloc_onnode(size, node); // Simplified
}

void NumaAllocator::free_aligned_node(void* ptr, size_t size, int node) {
    if (!is_available()) {
        free(ptr);
    } else {
        numa_free(ptr, size);
    }
}

// NumaBuffer implementation
NumaBuffer::NumaBuffer() : data_(nullptr), size_(0), node_(-1) {}

NumaBuffer::NumaBuffer(size_t size, int node) : data_(nullptr), size_(0), node_(node) {
    allocate(size, node);
}

NumaBuffer::~NumaBuffer() {
    free();
}

bool NumaBuffer::allocate(size_t size, int node) {
    free();
    node_ = node >= 0 ? node : NumaAllocator::get_preferred_node();
    data_ = NumaAllocator::allocate_node(size, node_);
    if (data_) {
        size_ = size;
        return true;
    }
    return false;
}

void NumaBuffer::free() {
    if (data_) {
        NumaAllocator::free_node(data_, size_, node_);
        data_ = nullptr;
        size_ = 0;
    }
}

bool NumaBuffer::migrate(int target_node) {
    if (!data_ || !NumaAllocator::is_available()) return false;
    return NumaAllocator::migrate(data_, size_, target_node);
}

int NumaBuffer::get_memory_node() const {
    if (!data_) return -1;
    return NumaAllocator::get_memory_node(data_);
}

// NumaStatsCollector static members
std::atomic<size_t> NumaStatsCollector::total_allocations_{0};
std::atomic<size_t> NumaStatsCollector::total_deallocations_{0};
std::atomic<size_t> NumaStatsCollector::total_allocated_bytes_{0};
std::atomic<size_t> NumaStatsCollector::current_usage_{0};
std::atomic<size_t> NumaStatsCollector::migrations_{0};
std::atomic<size_t> NumaStatsCollector::local_allocations_{0};
std::atomic<size_t> NumaStatsCollector::remote_allocations_{0};

NumaStats NumaStatsCollector::get_stats() {
    NumaStats stats;
    stats.total_allocations = total_allocations_.load();
    stats.total_deallocations = total_deallocations_.load();
    stats.total_allocated_bytes = total_allocated_bytes_.load();
    stats.current_usage = current_usage_.load();
    stats.migrations = migrations_.load();
    stats.local_allocations = local_allocations_.load();
    stats.remote_allocations = remote_allocations_.load();
    return stats;
}

void NumaStatsCollector::reset_stats() {
    total_allocations_ = 0;
    total_deallocations_ = 0;
    total_allocated_bytes_ = 0;
    current_usage_ = 0;
    migrations_ = 0;
    local_allocations_ = 0;
    remote_allocations_ = 0;
}

int get_cpu_to_node(int cpu) {
    if (!NumaAllocator::is_available()) return 0;
    return numa_node_of_cpu(cpu);
}

int get_node_distance(int node1, int node2) {
    if (!NumaAllocator::is_available()) return 0;
    return numa_distance(node1, node2);
}

int find_closest_node(int preferred_node) {
    if (!NumaAllocator::is_available()) return preferred_node >= 0 ? preferred_node : 0;
    
    int num_nodes = NumaAllocator::get_num_nodes();
    if (preferred_node < 0 || preferred_node >= num_nodes) {
        return 0;
    }
    
    return preferred_node;
}

int get_node_for_address(void* addr) {
    if (!NumaAllocator::is_available()) return 0;
    return NumaAllocator::get_memory_node(addr);
}

bool is_numa_capable() {
    return NumaAllocator::is_available() && NumaAllocator::get_num_nodes() > 1;
}

int get_optimal_node(size_t size, bool prefer_local) {
    if (!is_numa_capable()) return 0;
    
    if (prefer_local) {
        return NumaAllocator::get_preferred_node();
    }
    
    // Simple heuristic: allocate on node with most free memory
    int num_nodes = NumaAllocator::get_num_nodes();
    int best_node = 0;
    size_t max_free = 0;
    
    for (int i = 0; i < num_nodes; ++i) {
        NumaNodeInfo info;
        if (NumaAllocator::get_node_info(i, info)) {
            if (info.free_memory > max_free) {
                max_free = info.free_memory;
                best_node = i;
            }
        }
    }
    
    return best_node;
}

} // namespace memory
} // namespace best_server

#else

// Fallback implementation for systems without NUMA
namespace best_server {
namespace memory {

bool NumaAllocator::is_available() { return false; }
int NumaAllocator::get_num_nodes() { return 1; }
int NumaAllocator::get_preferred_node() { return 0; }
bool NumaAllocator::set_preferred_node(int) { return false; }
bool NumaAllocator::get_node_info(int, NumaNodeInfo&) { return false; }
void* NumaAllocator::allocate_node(size_t size, int) { return malloc(size); }
void NumaAllocator::free_node(void* ptr, size_t, int) { free(ptr); }
void* NumaAllocator::allocate_local(size_t size) { return malloc(size); }
void NumaAllocator::free_local(void* ptr, size_t) { free(ptr); }
bool NumaAllocator::set_policy(NumaPolicy, int) { return false; }
NumaPolicy NumaAllocator::get_policy() { return NumaPolicy::Default; }
bool NumaAllocator::migrate(void*, size_t, int) { return false; }
int NumaAllocator::get_memory_node(void*) { return 0; }
void* NumaAllocator::allocate_aligned_node(size_t size, size_t alignment, int) { 
    // Fallback: use malloc with manual alignment
    void* ptr = malloc(size + alignment + sizeof(void*));
    if (ptr) {
        uintptr_t addr = reinterpret_cast<uintptr_t>(ptr);
        uintptr_t aligned = (addr + sizeof(void*) + alignment - 1) & ~(alignment - 1);
        void** aligned_ptr = reinterpret_cast<void**>(aligned);
        aligned_ptr[-1] = ptr; // Store original pointer for deallocation
        return aligned_ptr;
    }
    return ptr;
}

void NumaAllocator::free_aligned_node(void* ptr, size_t, int) { 
    if (ptr) {
        void** aligned_ptr = static_cast<void**>(ptr);
        void* original = aligned_ptr[-1];
        ::free(original);
    }
}

// NumaBuffer implementation
NumaBuffer::NumaBuffer() : data_(nullptr), size_(0), node_(-1) {}
NumaBuffer::NumaBuffer(size_t size, int) : data_(nullptr), size_(0), node_(-1) { allocate(size); }
NumaBuffer::~NumaBuffer() { free(); }
bool NumaBuffer::allocate(size_t size, int) { 
    free(); 
    data_ = malloc(size); 
    if (data_) { size_ = size; return true; }
    return false; 
}
void NumaBuffer::free() { 
    if (data_) { 
        ::free(data_); 
        data_ = nullptr; 
        size_ = 0; 
    } 
}
bool NumaBuffer::migrate(int) { return false; }
int NumaBuffer::get_memory_node() const { return 0; }

// NumaStatsCollector static members
std::atomic<size_t> NumaStatsCollector::total_allocations_{0};
std::atomic<size_t> NumaStatsCollector::total_deallocations_{0};
std::atomic<size_t> NumaStatsCollector::total_allocated_bytes_{0};
std::atomic<size_t> NumaStatsCollector::current_usage_{0};
std::atomic<size_t> NumaStatsCollector::migrations_{0};
std::atomic<size_t> NumaStatsCollector::local_allocations_{0};
std::atomic<size_t> NumaStatsCollector::remote_allocations_{0};

NumaStats NumaStatsCollector::get_stats() {
    NumaStats stats;
    stats.total_allocations = total_allocations_.load();
    stats.total_deallocations = total_deallocations_.load();
    stats.total_allocated_bytes = total_allocated_bytes_.load();
    stats.current_usage = current_usage_.load();
    stats.migrations = migrations_.load();
    stats.local_allocations = local_allocations_.load();
    stats.remote_allocations = remote_allocations_.load();
    return stats;
}

void NumaStatsCollector::reset_stats() {
    total_allocations_ = 0;
    total_deallocations_ = 0;
    total_allocated_bytes_ = 0;
    current_usage_ = 0;
    migrations_ = 0;
    local_allocations_ = 0;
    remote_allocations_ = 0;
}

int get_cpu_to_node(int) { return 0; }
int get_node_distance(int, int) { return 0; }
int find_closest_node(int preferred_node) { return preferred_node >= 0 ? preferred_node : 0; }
int get_node_for_address(void*) { return 0; }
bool is_numa_capable() { return false; }
int get_optimal_node(size_t, bool) { return 0; }

} // namespace memory
} // namespace best_server

#endif