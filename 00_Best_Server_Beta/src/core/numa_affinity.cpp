// NUMA Affinity Manager implementation
#include "best_server/core/numa_affinity.hpp"
#include <fstream>
#include <sstream>
#include <iostream>
#include <algorithm>
#include <cstring>
#include <unordered_set>
#include <dirent.h>

#ifdef HAVE_LIBNUMA
#include <numa.h>
#endif

namespace best_server {
namespace core {

// ==================== NUMAAffinityManager Implementation ====================

NUMAAffinityManager::NUMAAffinityManager() {
    detect_topology();
}

NUMAAffinityManager::~NUMAAffinityManager() {
    // 清理资源
}

bool NUMAAffinityManager::detect_topology() {
    if (detected_) {
        return true;
    }
    
    // 获取CPU核心数
    topology_.num_cores = sysconf(_SC_NPROCESSORS_ONLN);
    
    // 初始化核心分配状态
    core_allocated_.resize(topology_.num_cores, false);
    
    // 读取拓扑信息
    bool success = read_sysfs_topology();
    if (success) {
        detected_ = true;
        return true;
    }
    
    // 如果sysfs失败，使用默认配置
    topology_.num_nodes = 1;
    topology_.num_sockets = 1;
    
    for (int i = 0; i < topology_.num_cores; ++i) {
        CPUCore core;
        core.core_id = i;
        core.numa_node = 0;
        core.physical_id = 0;
        core.hyperthread = 0;
        
        topology_.cores.push_back(core);
        topology_.cores_by_node[0].push_back(i);
    }
    
    NUMANode node;
    node.node_id = 0;
    node.cores = topology_.cores_by_node[0];
    node.sockets = {0};
    node.memory_size = 0;
    
    topology_.nodes.push_back(node);
    
    detected_ = true;
    return true;
}

bool NUMAAffinityManager::read_sysfs_topology() {
    // 读取CPU拓扑信息
    if (!read_cpu_info()) {
        return false;
    }
    
    // 读取NUMA信息
    read_numa_info();
    
    // 创建核心映射
    create_core_mapping();
    
    return true;
}

bool NUMAAffinityManager::read_cpu_info() {
    // 遍历 /sys/devices/system/cpu/cpuX
    DIR* cpu_dir = opendir("/sys/devices/system/cpu");
    if (!cpu_dir) {
        return false;
    }
    
    struct dirent* entry;
    while ((entry = readdir(cpu_dir)) != nullptr) {
        if (strncmp(entry->d_name, "cpu", 3) != 0) {
            continue;
        }
        
        int core_id = atoi(entry->d_name + 3);
        if (core_id < 0) {
            continue;
        }
        
        CPUCore core;
        core.core_id = core_id;
        core.numa_node = 0;
        core.physical_id = 0;
        core.hyperthread = 0;
        
        // 读取NUMA节点信息
        char path[256];
        snprintf(path, sizeof(path), "/sys/devices/system/cpu/cpu%d/topology/physical_package_id", core_id);
        std::ifstream phys_file(path);
        if (phys_file.is_open()) {
            phys_file >> core.physical_id;
        }
        
        // 读取NUMA节点
        snprintf(path, sizeof(path), "/sys/devices/system/node/node*/cpu%d", core_id);
        // 简化：默认node 0
        
        topology_.cores.push_back(core);
    }
    
    closedir(cpu_dir);
    
    return !topology_.cores.empty();
}

bool NUMAAffinityManager::read_numa_info() {
#ifdef HAVE_LIBNUMA
    if (numa_available() >= 0) {
        topology_.num_nodes = numa_num_configured_nodes();
        
        for (int i = 0; i < topology_.num_nodes; ++i) {
            NUMANode node;
            node.node_id = i;
            node.memory_size = numa_node_size64(i, nullptr);
            
            // 获取该节点的CPU核心
            bitmask* cpumask = numa_allocate_cpumask();
            numa_node_cpumask(i, cpumask);
            
            for (size_t j = 0; j < topology_.cores.size(); ++j) {
                if (numa_bitmask_isbitset(cpumask, j)) {
                    topology_.cores[j].numa_node = i;
                    node.cores.push_back(j);
                }
            }
            
            topology_.nodes.push_back(node);
            numa_free_cpumask(cpumask);
        }
        
        return true;
    }
#endif
    
    // 如果NUMA不可用，使用单节点
    topology_.num_nodes = 1;
    return false;
}

void NUMAAffinityManager::create_core_mapping() {
    // 按NUMA节点分组核心
    for (const auto& core : topology_.cores) {
        topology_.cores_by_node[core.numa_node].push_back(core.core_id);
    }
    
    // 统计Socket数
    std::unordered_set<int> sockets;
    for (const auto& core : topology_.cores) {
        sockets.insert(core.physical_id);
        topology_.nodes[core.numa_node].sockets.push_back(core.physical_id);
    }
    topology_.num_sockets = sockets.size();
}

bool NUMAAffinityManager::bind_thread_to_core(std::thread&, int core_id) {
    if (core_id < 0 || core_id >= topology_.num_cores) {
        return false;
    }
    
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    CPU_SET(core_id, &cpuset);
    
    return sched_setaffinity(0, sizeof(cpu_set_t), &cpuset) == 0;
}

bool NUMAAffinityManager::bind_thread_to_node(std::thread&, int node_id) {
    if (node_id < 0 || node_id >= topology_.num_nodes) {
        return false;
    }
    
    const auto& cores = topology_.cores_by_node[node_id];
    if (cores.empty()) {
        return false;
    }
    
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    
    for (int core : cores) {
        CPU_SET(core, &cpuset);
    }
    
    return sched_setaffinity(0, sizeof(cpu_set_t), &cpuset) == 0;
}

int NUMAAffinityManager::allocate_core(BindingStrategy strategy) {
    std::lock_guard<std::mutex> lock(allocation_mutex_);
    
    switch (strategy) {
        case BindingStrategy::ROUND_ROBIN: {
            for (int i = 0; i < topology_.num_cores; ++i) {
                int core = (next_core_++) % topology_.num_cores;
                if (!core_allocated_[core]) {
                    core_allocated_[core] = true;
                    return core;
                }
            }
            break;
        }
        
        case BindingStrategy::BALANCED: {
            // 选择负载最轻的核心
            int best_core = -1;
            
            for (int i = 0; i < topology_.num_cores; ++i) {
                if (!core_allocated_[i]) {
                    best_core = i;
                    break;
                }
            }
            
            if (best_core >= 0) {
                core_allocated_[best_core] = true;
                return best_core;
            }
            break;
        }
        
        case BindingStrategy::LOCALITY_FIRST:
        case BindingStrategy::CACHE_AWARE: {
            // 优先分配当前NUMA节点的核心
            int current_node = 0;
            const auto& cores = topology_.cores_by_node[current_node];
            
            for (int core : cores) {
                if (!core_allocated_[core]) {
                    core_allocated_[core] = true;
                    return core;
                }
            }
            
            // 如果本地节点已满，分配其他节点
            return allocate_core(BindingStrategy::BALANCED);
        }
    }
    
    return -1;
}

void NUMAAffinityManager::release_core(int core_id) {
    std::lock_guard<std::mutex> lock(allocation_mutex_);
    
    if (core_id >= 0 && core_id < topology_.num_cores) {
        core_allocated_[core_id] = false;
    }
}

int NUMAAffinityManager::get_node_for_core(int core_id) const {
    if (core_id < 0 || core_id >= static_cast<int>(topology_.cores.size())) {
        return -1;
    }
    
    return topology_.cores[core_id].numa_node;
}

bool NUMAAffinityManager::set_thread_priority(std::thread& thread, int priority) {
    sched_param param;
    param.sched_priority = priority;
    
    return pthread_setschedparam(thread.native_handle(), SCHED_RR, &param) == 0;
}

bool NUMAAffinityManager::set_thread_scheduler(std::thread& thread, int policy, int priority) {
    sched_param param;
    param.sched_priority = priority;
    
    return pthread_setschedparam(thread.native_handle(), policy, &param) == 0;
}

void NUMAAffinityManager::print_topology() const {
    std::cout << "NUMA Topology Information:" << std::endl;
    std::cout << "  Total Cores: " << topology_.num_cores << std::endl;
    std::cout << "  NUMA Nodes: " << topology_.num_nodes << std::endl;
    std::cout << "  Sockets: " << topology_.num_sockets << std::endl;
    
    for (size_t i = 0; i < topology_.nodes.size(); ++i) {
        const auto& node = topology_.nodes[i];
        std::cout << "\n  Node " << i << ":" << std::endl;
        std::cout << "    Cores: ";
        for (int core : node.cores) {
            std::cout << core << " ";
        }
        std::cout << std::endl;
        std::cout << "    Memory: " << (node.memory_size / (1024 * 1024)) << " MB" << std::endl;
    }
    
    std::cout << "\n  Core Details:" << std::endl;
    for (const auto& core : topology_.cores) {
        std::cout << "    Core " << core.core_id 
                  << ": Node=" << core.numa_node 
                  << ", Socket=" << core.physical_id 
                  << ", HT=" << core.hyperthread << std::endl;
    }
}

} // namespace core
} // namespace best_server