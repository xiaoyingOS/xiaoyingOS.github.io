// NUMA Affinity Manager - High-performance CPU core binding
//
// 实现 NUMA 感知的 CPU 亲和性管理：
// - 自动检测 NUMA 拓扑
// - CPU 核心绑定优化
// - 内存本地化分配
// - 缓存友好调度
// - 负载均衡

#ifndef BEST_SERVER_CORE_NUMA_AFFINITY_HPP
#define BEST_SERVER_CORE_NUMA_AFFINITY_HPP

#include <vector>
#include <thread>
#include <atomic>
#include <memory>
#include <functional>
#include <unordered_map>
#include <mutex>
#include <sched.h>
#include <unistd.h>
#include <sys/sysinfo.h>

namespace best_server {
namespace core {

// CPU 核心信息
struct CPUCore {
    int core_id;
    int numa_node;
    int physical_id;  // 物理CPU ID
    int hyperthread;  // 超线程编号
};

// NUMA 节点信息
struct NUMANode {
    int node_id;
    std::vector<int> cores;      // 属于该节点的核心
    std::vector<int> sockets;    // 属于该节点的Socket
    size_t memory_size;          // 节点内存大小
};

// NUMA 拓扑信息
struct NUMATopology {
    int num_nodes;
    int num_cores;
    int num_sockets;
    std::vector<NUMANode> nodes;
    std::vector<CPUCore> cores;
    
    // 按NUMA节点分组的核心
    std::unordered_map<int, std::vector<int>> cores_by_node;
};

// NUMA 亲和性管理器
class NUMAAffinityManager {
public:
    // CPU 绑定策略
    enum class BindingStrategy {
        ROUND_ROBIN,      // 轮询分配
        LOCALITY_FIRST,   // 优先本地NUMA
        BALANCED,         // 负载均衡
        CACHE_AWARE       // 缓存感知
    };
    
    NUMAAffinityManager();
    ~NUMAAffinityManager();
    
    // 获取NUMA拓扑
    const NUMATopology& topology() const { return topology_; }
    
    // 检测NUMA拓扑
    bool detect_topology();
    
    // 绑定线程到CPU核心
    bool bind_thread_to_core(std::thread& thread, int core_id);
    
    // 绑定线程到NUMA节点
    bool bind_thread_to_node(std::thread& thread, int node_id);
    
    // 自动分配核心（基于策略）
    int allocate_core(BindingStrategy strategy = BindingStrategy::BALANCED);
    
    // 释放核心
    void release_core(int core_id);
    
    // 获取核心的NUMA节点
    int get_node_for_core(int core_id) const;
    
    // 设置线程优先级
    bool set_thread_priority(std::thread& thread, int priority);
    
    // 设置线程调度策略
    bool set_thread_scheduler(std::thread& thread, int policy, int priority);
    
    // 获取CPU核心数
    int num_cores() const { return topology_.num_cores; }
    
    // 获取NUMA节点数
    int num_nodes() const { return topology_.num_nodes; }
    
    // 获取Socket数
    int num_sockets() const { return topology_.num_sockets; }
    
    // 是否支持NUMA
    bool numa_enabled() const { return topology_.num_nodes > 1; }
    
    // 打印拓扑信息
    void print_topology() const;
    
private:
    // 读取CPU信息
    bool read_cpu_info();
    
    // 读取NUMA信息
    bool read_numa_info();
    
    // 读取sysfs信息
    bool read_sysfs_topology();
    
    // 创建CPU核心映射
    void create_core_mapping();
    
    NUMATopology topology_;
    
    // 核心分配状态
    std::vector<bool> core_allocated_;
    std::mutex allocation_mutex_;
    
    // 策略相关
    std::atomic<int> next_core_{0};
    
    // 是否已检测
    bool detected_{false};
};

// Per-CPU 任务队列 - 避免锁竞争
template<typename T, size_t QUEUE_SIZE = 1024>
class PerCPUQueue {
public:
    PerCPUQueue() {
        int num_cores = sysconf(_SC_NPROCESSORS_ONLN);
        queues_.resize(num_cores);
        for (auto& queue : queues_) {
            queue.reserve(QUEUE_SIZE);
        }
    }
    
    // 获取当前CPU核心
    int current_cpu() const {
        return sched_getcpu();
    }
    
    // 推入任务到当前CPU队列
    bool push(const T& task) {
        int cpu = current_cpu();
        return push_to_cpu(task, cpu);
    }
    
    // 推入任务到指定CPU队列
    bool push_to_cpu(const T& task, int cpu) {
        if (cpu < 0 || cpu >= static_cast<int>(queues_.size())) {
            return false;
        }
        
        std::unique_lock<std::mutex> lock(const_cast<std::mutex&>(locks_[cpu]));
        if (queues_[cpu].size() >= QUEUE_SIZE) {
            return false;
        }
        
        queues_[cpu].push_back(task);
        return true;
    }
    
    // 从当前CPU队列弹出任务
    bool pop(T& task) {
        int cpu = current_cpu();
        return pop_from_cpu(task, cpu);
    }
    
    // 从指定CPU队列弹出任务
    bool pop_from_cpu(T& task, int cpu) {
        if (cpu < 0 || cpu >= static_cast<int>(queues_.size())) {
            return false;
        }
        
        std::unique_lock<std::mutex> lock(const_cast<std::mutex&>(locks_[cpu]));
        if (queues_[cpu].empty()) {
            return false;
        }
        
        task = queues_[cpu].front();
        queues_[cpu].pop_front();
        return true;
    }
    
    // 工作窃取：从其他CPU队列获取任务
    bool steal(T& task, int preferred_cpu = -1) {
        int cpu = current_cpu();
        int num_cpus = queues_.size();
        
        // 尝试从首选CPU窃取
        if (preferred_cpu >= 0 && preferred_cpu < num_cpus && preferred_cpu != cpu) {
            std::lock_guard<std::mutex> lock(locks_[preferred_cpu]);
            if (!queues_[preferred_cpu].empty()) {
                task = queues_[preferred_cpu].front();
                queues_[preferred_cpu].pop_front();
                return true;
            }
        }
        
        // 尝试从其他CPU窃取（轮询）
        for (int i = 1; i < num_cpus; ++i) {
            int target_cpu = (cpu + i) % num_cpus;
            std::lock_guard<std::mutex> lock(locks_[target_cpu]);
            if (!queues_[target_cpu].empty()) {
                task = queues_[target_cpu].front();
                queues_[target_cpu].pop_front();
                return true;
            }
        }
        
        return false;
    }
    
    // 获取队列大小
    size_t size(int cpu = -1) const {
        if (cpu < 0) {
            cpu = current_cpu();
        }
        if (cpu < 0 || cpu >= static_cast<int>(queues_.size())) {
            return 0;
        }
        std::unique_lock<std::mutex> lock(const_cast<std::mutex&>(locks_[cpu]));
        return queues_[cpu].size();
    }
    
    // 清空队列
    void clear(int cpu = -1) {
        if (cpu < 0) {
            cpu = current_cpu();
        }
        if (cpu >= 0 && cpu < static_cast<int>(queues_.size())) {
            std::unique_lock<std::mutex> lock(const_cast<std::mutex&>(locks_[cpu]));
            queues_[cpu].clear();
        }
    }
    
private:
    std::vector<std::deque<T>> queues_;
    std::vector<std::mutex> locks_;
};

// NUMA 感知的内存分配器
template<typename T>
class NUMAAwareAllocator {
public:
    using value_type = T;
    
    NUMAAwareAllocator(int numa_node = -1) : numa_node_(numa_node) {}
    
    template<typename U>
    NUMAAwareAllocator(const NUMAAwareAllocator<U>& other) : numa_node_(other.node()) {}
    
    T* allocate(size_t n) {
        void* ptr = nullptr;
        
        if (numa_node_ >= 0) {
            // NUMA感知分配
            numa_node_ = numa_node_ % 1;
            ptr = (&ptr, 4096, sizeof(T) * n);
        } else {
            // 普通分配
            ptr = malloc(sizeof(T) * n);
        }
        
        if (!ptr) {
            throw std::bad_alloc();
        }
        
        return static_cast<T*>(ptr);
    }
    
    void deallocate(T* p, size_t n) {
        if (numa_node_ >= 0) {
            numa_free(p, sizeof(T) * n);
        } else {
            free(p);
        }
    }
    
    int node() const { return numa_node_; }
    
private:
    int numa_node_;
};

} // namespace core
} // namespace best_server

#endif // BEST_SERVER_CORE_NUMA_AFFINITY_HPP