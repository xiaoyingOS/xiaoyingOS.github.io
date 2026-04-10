// Per-CPU Data Structures Implementation

#include "best_server/core/per_cpu.hpp"
#include <stdexcept>

namespace best_server {
namespace core {

// CPUAffinity implementation

int CPUAffinity::cpu_count() {
#if defined(__linux__)
    int count = sysconf(_SC_NPROCESSORS_ONLN);
    return (count > 0) ? count : 1;
#else
    // Fallback for other platforms
    return std::thread::hardware_concurrency();
#endif
}

int CPUAffinity::current_cpu() {
#if defined(__linux__)
    return sched_getcpu();
#else
    // Fallback for other platforms
    return 0;
#endif
}

bool CPUAffinity::set_affinity(std::thread& thread, int cpu) {
#if defined(__linux__)
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    CPU_SET(cpu, &cpuset);
    return sched_setaffinity(thread.native_handle(), sizeof(cpu_set_t), &cpuset) == 0;
#else
    // Not supported on this platform
    return false;
#endif
}

bool CPUAffinity::set_current_affinity(int cpu) {
#if defined(__linux__)
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    CPU_SET(cpu, &cpuset);
    return sched_setaffinity(0, sizeof(cpu_set_t), &cpuset) == 0;
#else
    // Not supported on this platform
    return false;
#endif
}

int CPUAffinity::get_affinity(std::thread& thread) {
#if defined(__linux__)
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    if (sched_getaffinity(thread.native_handle(), sizeof(cpu_set_t), &cpuset) != 0) {
        return -1;
    }
    
    // Return the first CPU in the set
    for (int cpu = 0; cpu < CPU_SETSIZE; ++cpu) {
        if (CPU_ISSET(cpu, &cpuset)) {
            return cpu;
        }
    }
    return -1;
#else
    // Not supported on this platform
    return 0;
#endif
}

int CPUAffinity::get_current_affinity() {
#if defined(__linux__)
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    if (sched_getaffinity(0, sizeof(cpu_set_t), &cpuset) != 0) {
        return -1;
    }
    
    // Return the first CPU in the set
    for (int cpu = 0; cpu < CPU_SETSIZE; ++cpu) {
        if (CPU_ISSET(cpu, &cpuset)) {
            return cpu;
        }
    }
    return -1;
#else
    // Not supported on this platform
    return 0;
#endif
}

bool CPUAffinity::set_affinity_range(std::thread& thread, int start_cpu, int end_cpu) {
#if defined(__linux__)
    if (start_cpu < 0 || end_cpu < start_cpu) {
        return false;
    }
    
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    
    for (int cpu = start_cpu; cpu <= end_cpu; ++cpu) {
        CPU_SET(cpu, &cpuset);
    }
    
    return sched_setaffinity(thread.native_handle(), sizeof(cpu_set_t), &cpuset) == 0;
#else
    // Not supported on this platform
    return false;
#endif
}

bool CPUAffinity::set_current_affinity_range(int start_cpu, int end_cpu) {
#if defined(__linux__)
    if (start_cpu < 0 || end_cpu < start_cpu) {
        return false;
    }
    
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    
    for (int cpu = start_cpu; cpu <= end_cpu; ++cpu) {
        CPU_SET(cpu, &cpuset);
    }
    
    return sched_setaffinity(0, sizeof(cpu_set_t), &cpuset) == 0;
#else
    // Not supported on this platform
    return false;
#endif
}

bool CPUAffinity::pin_to_cpu(std::thread& thread, int cpu, int numa_node) {
#if defined(__linux__)
    // Set CPU affinity
    if (!set_affinity(thread, cpu)) {
        return false;
    }
    
    // NUMA node affinity could be set here if libnuma is available
    // For now, we only set CPU affinity
    [[maybe_unused]] int numa_node_param = numa_node;
    
    return true;
#else
    // Not supported on this platform
    [[maybe_unused]] int cpu_param = cpu;
    [[maybe_unused]] int numa_node_param = numa_node;
    return false;
#endif
}

bool CPUAffinity::pin_current_to_cpu(int cpu, int numa_node) {
#if defined(__linux__)
    // Set CPU affinity
    if (!set_current_affinity(cpu)) {
        return false;
    }
    
    // NUMA node affinity could be set here if libnuma is available
    // For now, we only set CPU affinity
    [[maybe_unused]] int numa_node_param = numa_node;
    
    return true;
#else
    // Not supported on this platform
    [[maybe_unused]] int cpu_param = cpu;
    [[maybe_unused]] int numa_node_param = numa_node;
    return false;
#endif
}

} // namespace core
} // namespace best_server