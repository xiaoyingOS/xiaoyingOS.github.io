// Quick Performance Test
// Simplified performance test

#include <iostream>
#include <chrono>
#include <iomanip>

#include "best_server/core/lockfree_queue.hpp"
#include "best_server/core/per_cpu.hpp"
#include "best_server/core/cache_optimization.hpp"

using namespace best_server;
using namespace std::chrono;

class Timer {
public:
    Timer() : start_(high_resolution_clock::now()) {}
    
    void reset() {
        start_ = high_resolution_clock::now();
    }
    
    double elapsed_ms() const {
        return duration_cast<microseconds>(high_resolution_clock::now() - start_).count() / 1000.0;
    }
    
private:
    high_resolution_clock::time_point start_;
};

int main() {
    std::cout << "========================================" << std::endl;
    std::cout << "  Best Server Quick Performance Test" << std::endl;
    std::cout << "========================================" << std::endl;
    
    // Test 1: CPU Affinity
    std::cout << "\n=== CPU Affinity Test ===" << std::endl;
    int cpu_count = core::CPUAffinity::cpu_count();
    int current_cpu = core::CPUAffinity::current_cpu();
    std::cout << "CPU Count: " << cpu_count << std::endl;
    std::cout << "Current CPU: " << current_cpu << std::endl;
    
    // Test 2: Per-CPU Counter
    std::cout << "\n=== Per-CPU Counter Test ===" << std::endl;
    const size_t iterations = 1000000;
    core::PerCPUCounter<uint64_t, 128> counter;
    
    Timer timer;
    for (size_t i = 0; i < iterations; ++i) {
        counter.inc(1);
    }
    double inc_time = timer.elapsed_ms();
    
    uint64_t total = counter.sum();
    std::cout << "Increment: " << std::fixed << std::setprecision(2) 
              << (iterations / inc_time) << " ops/ms" << std::endl;
    std::cout << "Total: " << total << std::endl;
    
    // Test 3: Cache Aligned Counter
    std::cout << "\n=== Cache Aligned Counter Test ===" << std::endl;
    core::CacheLineAlignedCounter<uint64_t> cache_counter;
    
    timer.reset();
    for (size_t i = 0; i < iterations; ++i) {
        cache_counter.inc(1);
    }
    double cache_inc_time = timer.elapsed_ms();
    
    uint64_t cache_value = cache_counter.get();
    std::cout << "Increment: " << std::fixed << std::setprecision(2) 
              << (iterations / cache_inc_time) << " ops/ms" << std::endl;
    std::cout << "Value: " << cache_value << std::endl;
    
    // Test 4: Lock-Free Queue
    std::cout << "\n=== Lock-Free Queue Test ===" << std::endl;
    const size_t queue_iterations = 10000;
    core::LockFreeQueue<int, 1024> queue;
    
    timer.reset();
    for (size_t i = 0; i < queue_iterations; ++i) {
        while (!queue.try_push(static_cast<int>(i))) {
            std::this_thread::yield();
        }
    }
    double push_time = timer.elapsed_ms();
    
    timer.reset();
    int value;
    for (size_t i = 0; i < queue_iterations; ++i) {
        while (!queue.try_pop(value)) {
            std::this_thread::yield();
        }
    }
    double pop_time = timer.elapsed_ms();
    
    std::cout << "Push: " << std::fixed << std::setprecision(2) 
              << (queue_iterations / push_time) << " ops/ms" << std::endl;
    std::cout << "Pop:  " << std::fixed << std::setprecision(2) 
              << (queue_iterations / pop_time) << " ops/ms" << std::endl;
    
    // Test 5: Ring Buffer
    std::cout << "\n=== Ring Buffer Test ===" << std::endl;
    const size_t ring_iterations = 10000;
    core::CacheOptimizedRingBuffer<int, 1024> ring;
    
    timer.reset();
    for (size_t i = 0; i < ring_iterations; ++i) {
        while (!ring.push(static_cast<int>(i))) {
            std::this_thread::yield();
        }
        int ring_value;
        while (!ring.pop(ring_value)) {
            std::this_thread::yield();
        }
    }
    double ring_time = timer.elapsed_ms();
    
    std::cout << "Push/Pop: " << std::fixed << std::setprecision(2) 
              << (2 * ring_iterations / ring_time) << " ops/ms" << std::endl;
    
    std::cout << "\n========================================" << std::endl;
    std::cout << "  All Tests Completed!" << std::endl;
    std::cout << "========================================" << std::endl;
    
    return 0;
}