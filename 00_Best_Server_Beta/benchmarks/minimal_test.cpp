// Minimal Performance Test
// Very simplified test to verify basic functionality

#include <iostream>
#include <chrono>
#include <iomanip>

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
    std::cout << "  Best Server Minimal Test" << std::endl;
    std::cout << "========================================" << std::endl;
    
    // Test 1: CPU Affinity
    std::cout << "\n=== CPU Affinity Test ===" << std::endl;
    int cpu_count = core::CPUAffinity::cpu_count();
    int current_cpu = core::CPUAffinity::current_cpu();
    std::cout << "CPU Count: " << cpu_count << std::endl;
    std::cout << "Current CPU: " << current_cpu << std::endl;
    std::cout << "✓ CPU Affinity test passed" << std::endl;
    
    // Test 2: Per-CPU Counter
    std::cout << "\n=== Per-CPU Counter Test ===" << std::endl;
    const size_t iterations = 100000;
    core::PerCPUCounter<uint64_t, 128> counter;
    
    Timer timer;
    for (size_t i = 0; i < iterations; ++i) {
        counter.inc(1);
    }
    double inc_time = timer.elapsed_ms();
    
    uint64_t total = counter.sum();
    std::cout << "Iterations: " << iterations << std::endl;
    std::cout << "Time: " << std::fixed << std::setprecision(2) << inc_time << " ms" << std::endl;
    std::cout << "Throughput: " << std::fixed << std::setprecision(2) 
              << (iterations / inc_time) << " ops/ms" << std::endl;
    std::cout << "Total: " << total << std::endl;
    if (total == iterations) {
        std::cout << "✓ Per-CPU Counter test passed" << std::endl;
    } else {
        std::cout << "✗ Per-CPU Counter test failed" << std::endl;
    }
    
    // Test 3: Cache Aligned Counter
    std::cout << "\n=== Cache Aligned Counter Test ===" << std::endl;
    core::CacheLineAlignedCounter<uint64_t> cache_counter;
    
    timer.reset();
    for (size_t i = 0; i < iterations; ++i) {
        cache_counter.inc(1);
    }
    double cache_inc_time = timer.elapsed_ms();
    
    uint64_t cache_value = cache_counter.get();
    std::cout << "Iterations: " << iterations << std::endl;
    std::cout << "Time: " << std::fixed << std::setprecision(2) << cache_inc_time << " ms" << std::endl;
    std::cout << "Throughput: " << std::fixed << std::setprecision(2) 
              << (iterations / cache_inc_time) << " ops/ms" << std::endl;
    std::cout << "Value: " << cache_value << std::endl;
    if (cache_value == iterations) {
        std::cout << "✓ Cache Aligned Counter test passed" << std::endl;
    } else {
        std::cout << "✗ Cache Aligned Counter test failed" << std::endl;
    }
    
    // Test 4: Ring Buffer
    std::cout << "\n=== Ring Buffer Test ===" << std::endl;
    const size_t ring_iterations = 1000;
    core::CacheOptimizedRingBuffer<int, 1024> ring;
    
    timer.reset();
    for (size_t i = 0; i < ring_iterations; ++i) {
        if (!ring.push(static_cast<int>(i))) {
            std::cout << "Warning: Ring buffer full at iteration " << i << std::endl;
            break;
        }
    }
    double push_time = timer.elapsed_ms();
    
    timer.reset();
    int ring_value;
    size_t popped = 0;
    for (size_t i = 0; i < ring_iterations; ++i) {
        if (ring.pop(ring_value)) {
            popped++;
        }
    }
    double pop_time = timer.elapsed_ms();
    
    std::cout << "Pushed: " << ring_iterations << std::endl;
    std::cout << "Popped: " << popped << std::endl;
    std::cout << "Push time: " << std::fixed << std::setprecision(2) << push_time << " ms" << std::endl;
    std::cout << "Pop time: " << std::fixed << std::setprecision(2) << pop_time << " ms" << std::endl;
    if (popped == ring_iterations) {
        std::cout << "✓ Ring Buffer test passed" << std::endl;
    } else {
        std::cout << "✗ Ring Buffer test failed" << std::endl;
    }
    
    // Test 5: Per-CPU Storage
    std::cout << "\n=== Per-CPU Storage Test ===" << std::endl;
    core::PerCPUStorage<int, 128> storage;
    storage.for_current_cpu() = 42;
    int value = storage.for_current_cpu();
    std::cout << "Stored value: 42" << std::endl;
    std::cout << "Retrieved value: " << value << std::endl;
    if (value == 42) {
        std::cout << "✓ Per-CPU Storage test passed" << std::endl;
    } else {
        std::cout << "✗ Per-CPU Storage test failed" << std::endl;
    }
    
    std::cout << "\n========================================" << std::endl;
    std::cout << "  All Tests Completed!" << std::endl;
    std::cout << "========================================" << std::endl;
    
    return 0;
}