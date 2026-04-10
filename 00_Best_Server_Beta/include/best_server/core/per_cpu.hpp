// Per-CPU Data Structures - CPU affinity and per-core data management
//
// Implements per-CPU data structures for:
// - CPU affinity management
// - Per-CPU data storage
// - CPU core binding
// - NUMA node awareness

#ifndef BEST_SERVER_CORE_PER_CPU_HPP
#define BEST_SERVER_CORE_PER_CPU_HPP

#include <array>
#include <atomic>
#include <thread>
#include <cstdint>
#include <cstring>

#if defined(__linux__)
    #include <sched.h>
    #include <unistd.h>
#endif

namespace best_server {
namespace core {

// CPU affinity manager
class CPUAffinity {
public:
    // Get number of CPU cores
    static int cpu_count();
    
    // Get current CPU ID
    static int current_cpu();
    
    // Set thread affinity to specific CPU
    static bool set_affinity(std::thread& thread, int cpu);
    
    // Set current thread affinity
    static bool set_current_affinity(int cpu);
    
    // Get thread affinity
    static int get_affinity(std::thread& thread);
    
    // Get current thread affinity
    static int get_current_affinity();
    
    // Set thread affinity to a range of CPUs
    static bool set_affinity_range(std::thread& thread, int start_cpu, int end_cpu);
    
    // Set current thread affinity to a range
    static bool set_current_affinity_range(int start_cpu, int end_cpu);
    
    // Pin thread to CPU (with optional NUMA node)
    static bool pin_to_cpu(std::thread& thread, int cpu, int numa_node = -1);
    
    // Pin current thread to CPU
    static bool pin_current_to_cpu(int cpu, int numa_node = -1);
};

// Per-CPU data storage (cache-line aligned)
template<typename T, size_t MaxCPUs = 128>
class PerCPUStorage {
public:
    static constexpr size_t CACHE_LINE_SIZE = 64;
    
    PerCPUStorage() : cpu_count_(CPUAffinity::cpu_count()) {
        // Initialize all per-CPU data
        for (size_t i = 0; i < MaxCPUs; ++i) {
            new (&data_[i]) T();
        }
    }
    
    ~PerCPUStorage() {
        // Destroy all per-CPU data
        for (size_t i = 0; i < MaxCPUs; ++i) {
            reinterpret_cast<T*>(&data_[i])->~T();
        }
    }
    
    // Get data for current CPU
    T& for_current_cpu() {
        int cpu = CPUAffinity::current_cpu();
        if (cpu < 0 || cpu >= static_cast<int>(cpu_count_)) {
            cpu = 0; // Fallback to CPU 0
        }
        return *reinterpret_cast<T*>(&data_[cpu]);
    }
    
    const T& for_current_cpu() const {
        int cpu = CPUAffinity::current_cpu();
        if (cpu < 0 || cpu >= static_cast<int>(cpu_count_)) {
            cpu = 0; // Fallback to CPU 0
        }
        return *reinterpret_cast<const T*>(&data_[cpu]);
    }
    
    // Get data for specific CPU
    T& for_cpu(int cpu) {
        if (cpu < 0 || cpu >= static_cast<int>(cpu_count_)) {
            cpu = 0; // Fallback to CPU 0
        }
        return *reinterpret_cast<T*>(&data_[cpu]);
    }
    
    const T& for_cpu(int cpu) const {
        if (cpu < 0 || cpu >= static_cast<int>(cpu_count_)) {
            cpu = 0; // Fallback to CPU 0
        }
        return *reinterpret_cast<const T*>(&data_[cpu]);
    }
    
    // Get number of CPUs
    size_t cpu_count() const { return cpu_count_; }
    
    // Iterate over all CPUs
    template<typename F>
    void for_each_cpu(F&& func) {
        for (size_t i = 0; i < cpu_count_; ++i) {
            func(data_[i], static_cast<int>(i));
        }
    }
    
    template<typename F>
    void for_each_cpu(F&& func) const {
        for (size_t i = 0; i < cpu_count_; ++i) {
            func(data_[i], static_cast<int>(i));
        }
    }
    
private:
    // Cache-line aligned storage for each CPU
    struct alignas(CACHE_LINE_SIZE) AlignedData {
        char storage[sizeof(T)];
        
        T* get() { return reinterpret_cast<T*>(storage); }
        const T* get() const { return reinterpret_cast<const T*>(storage); }
    };
    
    std::array<AlignedData, MaxCPUs> data_;
    size_t cpu_count_;
};

// Per-CPU counter (atomic, lock-free)
template<typename T = uint64_t, size_t MaxCPUs = 128>
class PerCPUCounter {
public:
    PerCPUCounter() : value_() {}
    
    // Increment counter for current CPU
    void inc(T delta = 1) {
        int cpu = CPUAffinity::current_cpu();
        if (cpu < 0 || cpu >= static_cast<int>(MaxCPUs)) {
            cpu = 0;
        }
        value_[cpu].fetch_add(delta, std::memory_order_relaxed);
    }
    
    // Decrement counter for current CPU
    void dec(T delta = 1) {
        int cpu = CPUAffinity::current_cpu();
        if (cpu < 0 || cpu >= static_cast<int>(MaxCPUs)) {
            cpu = 0;
        }
        value_[cpu].fetch_sub(delta, std::memory_order_relaxed);
    }
    
    // Add to counter for specific CPU
    void add(int cpu, T delta) {
        if (cpu < 0 || cpu >= static_cast<int>(MaxCPUs)) {
            return;
        }
        value_[cpu].fetch_add(delta, std::memory_order_relaxed);
    }
    
    // Get counter for current CPU
    T get_current() const {
        int cpu = CPUAffinity::current_cpu();
        if (cpu < 0 || cpu >= static_cast<int>(MaxCPUs)) {
            cpu = 0;
        }
        return value_[cpu].load(std::memory_order_relaxed);
    }
    
    // Get counter for specific CPU
    T get(int cpu) const {
        if (cpu < 0 || cpu >= static_cast<int>(MaxCPUs)) {
            return 0;
        }
        return value_[cpu].load(std::memory_order_relaxed);
    }
    
    // Get sum of all per-CPU counters
    T sum() const {
        T total = 0;
        for (const auto& v : value_) {
            total += v.load(std::memory_order_relaxed);
        }
        return total;
    }
    
    // Reset all counters
    void reset() {
        for (auto& v : value_) {
            v.store(0, std::memory_order_relaxed);
        }
    }
    
private:
    alignas(64) std::array<std::atomic<T>, MaxCPUs> value_;
};

// Per-CPU statistics
struct PerCPUStats {
    std::atomic<uint64_t> tasks_processed{0};
    std::atomic<uint64_t> tasks_stolen{0};
    std::atomic<uint64_t> context_switches{0};
    std::atomic<uint64_t> cache_misses{0};
    std::atomic<uint64_t> lock_contentions{0};
};

// Per-CPU statistics collector
class PerCPUStatsCollector {
public:
    PerCPUStatsCollector() : stats_() {}
    
    // Get stats for current CPU
    PerCPUStats& current() {
        int cpu = CPUAffinity::current_cpu();
        if (cpu < 0 || cpu >= static_cast<int>(128)) {
            cpu = 0;
        }
        return stats_[cpu];
    }
    
    // Get stats for specific CPU
    PerCPUStats& get(int cpu) {
        if (cpu < 0 || cpu >= static_cast<int>(128)) {
            cpu = 0;
        }
        return stats_[cpu];
    }
    
    // Aggregate stats from all CPUs
    void aggregate(PerCPUStats& total) const {
        for (const auto& s : stats_) {
            total.tasks_processed.fetch_add(s.tasks_processed.load(std::memory_order_relaxed), 
                                           std::memory_order_relaxed);
            total.tasks_stolen.fetch_add(s.tasks_stolen.load(std::memory_order_relaxed), 
                                        std::memory_order_relaxed);
            total.context_switches.fetch_add(s.context_switches.load(std::memory_order_relaxed), 
                                            std::memory_order_relaxed);
            total.cache_misses.fetch_add(s.cache_misses.load(std::memory_order_relaxed), 
                                        std::memory_order_relaxed);
            total.lock_contentions.fetch_add(s.lock_contentions.load(std::memory_order_relaxed), 
                                            std::memory_order_relaxed);
        }
    }
    
private:
    alignas(64) std::array<PerCPUStats, 128> stats_;
};

} // namespace core
} // namespace best_server

#endif // BEST_SERVER_CORE_PER_CPU_HPP