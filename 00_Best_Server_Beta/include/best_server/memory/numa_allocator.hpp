// NUMA Awareness - NUMA node memory allocation
//
// Provides NUMA-aware memory allocation for multi-socket systems
// Note: NUMA support requires libnuma and NUMA-capable hardware

#ifndef BEST_SERVER_MEMORY_NUMA_ALLOCATOR_HPP
#define BEST_SERVER_MEMORY_NUMA_ALLOCATOR_HPP

#include <cstddef>
#include <cstdint>
#include <atomic>

namespace best_server {
namespace memory {

// NUMA node information
struct NumaNodeInfo {
    int node_id;          // NUMA node ID
    size_t total_memory;  // Total memory in bytes
    size_t free_memory;   // Free memory in bytes
    size_t cpus;          // Number of CPUs in this node
    int distance;         // Distance from current node
};

// NUMA memory policy
enum class NumaPolicy {
    Default,       // Use system default
    Local,         // Allocate on local node
    Interleave,    // Interleave across all nodes
    Preferred,     // Prefer specific node
    Bind,          // Bind to specific node
    Max
};

// NUMA-aware allocator
class NumaAllocator {
public:
    // Check if NUMA is available
    static bool is_available();
    
    // Get number of NUMA nodes
    static int get_num_nodes();
    
    // Get preferred NUMA node for current thread
    static int get_preferred_node();
    
    // Set preferred NUMA node for current thread
    static bool set_preferred_node(int node);
    
    // Get NUMA node information
    static bool get_node_info(int node, NumaNodeInfo& info);
    
    // Allocate memory on specific NUMA node
    static void* allocate_node(size_t size, int node);
    
    // Free memory allocated on NUMA node
    static void free_node(void* ptr, size_t size, int node);
    
    // Allocate memory on local NUMA node
    static void* allocate_local(size_t size);
    
    // Free memory allocated on local NUMA node
    static void free_local(void* ptr, size_t size);
    
    // Set NUMA memory policy
    static bool set_policy(NumaPolicy policy, int preferred_node = -1);
    
    // Get current NUMA memory policy
    static NumaPolicy get_policy();
    
    // Migrate memory to another NUMA node
    static bool migrate(void* ptr, size_t size, int target_node);
    
    // Get memory location (which NUMA node)
    static int get_memory_node(void* ptr);
    
    // Allocate aligned memory on NUMA node
    static void* allocate_aligned_node(size_t size, size_t alignment, int node);
    
    // Free aligned memory allocated on NUMA node
    static void free_aligned_node(void* ptr, size_t size, int node);
};

// Per-NUMA node allocator
template<typename T>
class NumaNodeAllocator {
public:
    using value_type = T;
    
    explicit NumaNodeAllocator(int node) : node_(node) {}
    
    template<typename U>
    NumaNodeAllocator(const NumaNodeAllocator<U>& other) : node_(other.node()) {}
    
    T* allocate(size_t n) {
        size_t size = n * sizeof(T);
        void* ptr = NumaAllocator::allocate_node(size, node_);
        return static_cast<T*>(ptr);
    }
    
    void deallocate(T* ptr, size_t n) {
        size_t size = n * sizeof(T);
        NumaAllocator::free_node(ptr, size, node_);
    }
    
    int node() const { return node_; }
    
private:
    int node_;
};

// NUMA-aware buffer
class NumaBuffer {
public:
    NumaBuffer();
    explicit NumaBuffer(size_t size, int node = -1);
    ~NumaBuffer();
    
    // Get data pointer
    void* data() { return data_; }
    const void* data() const { return data_; }
    
    // Get size
    size_t size() const { return size_; }
    
    // Get NUMA node
    int node() const { return node_; }
    
    // Check if empty
    bool empty() const { return data_ == nullptr; }
    
    // Allocate
    bool allocate(size_t size, int node = -1);
    
    // Free
    void free();
    
    // Migrate to another node
    bool migrate(int target_node);
    
    // Get actual memory node
    int get_memory_node() const;
    
private:
    void* data_;
    size_t size_;
    int node_;
};

// NUMA-aware object pool
template<typename T>
class NumaObjectPool {
public:
    explicit NumaObjectPool(int node = -1);
    ~NumaObjectPool();
    
    // Allocate object
    T* allocate();
    
    // Deallocate object
    void deallocate(T* obj);
    
    // Get pool size
    size_t size() const;
    
    // Get NUMA node
    int node() const { return node_; }
    
    // Clear pool
    void clear();
    
private:
    int node_;
    struct Node {
        T* object;
        Node* next;
    };
    Node* free_list_;
    std::atomic<size_t> pool_size_;
};

// NUMA statistics
struct NumaStats {
    size_t total_allocations;
    size_t total_deallocations;
    size_t total_allocated_bytes;
    size_t current_usage;
    size_t migrations;
    size_t local_allocations;
    size_t remote_allocations;
};

// Get NUMA statistics
class NumaStatsCollector {
public:
    static NumaStats get_stats();
    static void reset_stats();
    
private:
    static std::atomic<size_t> total_allocations_;
    static std::atomic<size_t> total_deallocations_;
    static std::atomic<size_t> total_allocated_bytes_;
    static std::atomic<size_t> current_usage_;
    static std::atomic<size_t> migrations_;
    static std::atomic<size_t> local_allocations_;
    static std::atomic<size_t> remote_allocations_;
};

// Helper functions

// Get CPU to NUMA node mapping
int get_cpu_to_node(int cpu);

// Get NUMA node distance
int get_node_distance(int node1, int node2);

// Find closest NUMA node
int find_closest_node(int preferred_node);

// Get NUMA node for memory address
int get_node_for_address(void* addr);

// Check if system is NUMA-capable
bool is_numa_capable();

// Get optimal node for allocation
int get_optimal_node(size_t size, bool prefer_local = true);

} // namespace memory
} // namespace best_server

#endif // BEST_SERVER_MEMORY_NUMA_ALLOCATOR_HPP