// Simple Performance Test
// Custom performance test without Google Benchmark dependency

#include <iostream>
#include <chrono>
#include <vector>
#include <thread>
#include <atomic>
#include <random>
#include <iomanip>

#include "best_server/core/lockfree_queue.hpp"
#include "best_server/memory/slab_allocator.hpp"
#include "best_server/core/per_cpu.hpp"
#include "best_server/core/cache_optimization.hpp"

using namespace best_server;
using namespace std::chrono;

// Timer helper
class Timer {
public:
    Timer() : start_(high_resolution_clock::now()) {}
    
    void reset() {
        start_ = high_resolution_clock::now();
    }
    
    double elapsed_ms() const {
        return duration_cast<microseconds>(high_resolution_clock::now() - start_).count() / 1000.0;
    }
    
    double elapsed_us() const {
        return duration_cast<microseconds>(high_resolution_clock::now() - start_).count();
    }
    
private:
    high_resolution_clock::time_point start_;
};

// Test 1: Queue Performance
void test_queue_performance() {
    std::cout << "\n=== Queue Performance Test ===" << std::endl;
    
    const size_t iterations = 10000;
    const size_t queue_size = 1024;
    
    // Test LockFreeQueue
    {
        core::LockFreeQueue<int, queue_size> queue;
        Timer timer;
        
        // Push test
        for (size_t i = 0; i < iterations; ++i) {
            while (!queue.try_push(static_cast<int>(i))) {
                std::this_thread::yield();
            }
        }
        double push_time = timer.elapsed_ms();
        
        // Pop test
        timer.reset();
        int value;
        for (size_t i = 0; i < iterations; ++i) {
            while (!queue.try_pop(value)) {
                std::this_thread::yield();
            }
        }
        double pop_time = timer.elapsed_ms();
        
        std::cout << "LockFreeQueue:" << std::endl;
        std::cout << "  Push: " << std::fixed << std::setprecision(2) 
                  << (iterations / push_time) << " ops/ms" << std::endl;
        std::cout << "  Pop:  " << std::fixed << std::setprecision(2) 
                  << (iterations / pop_time) << " ops/ms" << std::endl;
        std::cout << "  Total: " << std::fixed << std::setprecision(2) 
                  << (2 * iterations / (push_time + pop_time)) << " ops/ms" << std::endl;
    }
}

// Test 2: Slab Allocator Performance
void test_slab_allocator_performance() {
    std::cout << "\n=== Slab Allocator Performance Test ===" << std::endl;
    
    const size_t iterations = 10000;
    const size_t sizes[] = {8, 16, 32, 64, 128, 256, 512, 1024};
    
    auto& allocator = memory::SlabAllocator::instance();
    
    for (size_t size : sizes) {
        std::vector<void*> ptrs;
        ptrs.reserve(iterations);
        
        Timer timer;
        
        // Allocation test
        for (size_t i = 0; i < iterations; ++i) {
            void* ptr = allocator.allocate(size);
            if (ptr) {
                ptrs.push_back(ptr);
            }
        }
        double alloc_time = timer.elapsed_ms();
        
        // Deallocation test
        timer.reset();
        for (void* ptr : ptrs) {
            allocator.deallocate(ptr, size);
        }
        double dealloc_time = timer.elapsed_ms();
        
        std::cout << "Size " << std::setw(4) << size << "B:" << std::endl;
        std::cout << "  Alloc:   " << std::fixed << std::setprecision(2) 
                  << (iterations / alloc_time) << " ops/ms" << std::endl;
        std::cout << "  Dealloc: " << std::fixed << std::setprecision(2) 
                  << (iterations / dealloc_time) << " ops/ms" << std::endl;
        std::cout << "  Total:   " << std::fixed << std::setprecision(2) 
                  << (2 * iterations / (alloc_time + dealloc_time)) << " ops/ms" << std::endl;
    }
}

// Test 3: Per-CPU Counter Performance
void test_per_cpu_counter_performance() {
    std::cout << "\n=== Per-CPU Counter Performance Test ===" << std::endl;
    
    const size_t iterations = 1000000;
    core::PerCPUCounter<uint64_t, 128> counter;
    
    Timer timer;
    
    // Increment test
    for (size_t i = 0; i < iterations; ++i) {
        counter.inc(1);
    }
    double inc_time = timer.elapsed_ms();
    
    // Sum test
    timer.reset();
    uint64_t total = counter.sum();
    double sum_time = timer.elapsed_ms();
    
    std::cout << "PerCPUCounter:" << std::endl;
    std::cout << "  Increment: " << std::fixed << std::setprecision(2) 
              << (iterations / inc_time) << " ops/ms" << std::endl;
    std::cout << "  Sum:       " << std::fixed << std::setprecision(2) 
              << (1 / sum_time) << " ops/ms" << std::endl;
    std::cout << "  Total:     " << std::fixed << std::setprecision(2) 
              << total << std::endl;
}

// Test 4: Cache-Line Aligned Counter Performance
void test_cache_aligned_counter_performance() {
    std::cout << "\n=== Cache Aligned Counter Performance Test ===" << std::endl;
    
    const size_t iterations = 1000000;
    core::CacheLineAlignedCounter<uint64_t> counter;
    
    Timer timer;
    
    // Increment test
    for (size_t i = 0; i < iterations; ++i) {
        counter.inc(1);
    }
    double inc_time = timer.elapsed_ms();
    
    uint64_t value = counter.get();
    
    std::cout << "CacheLineAlignedCounter:" << std::endl;
    std::cout << "  Increment: " << std::fixed << std::setprecision(2) 
              << (iterations / inc_time) << " ops/ms" << std::endl;
    std::cout << "  Value:     " << std::fixed << std::setprecision(2) 
              << value << std::endl;
}

// Test 5: Ring Buffer Performance
void test_ring_buffer_performance() {
    std::cout << "\n=== Ring Buffer Performance Test ===" << std::endl;
    
    const size_t iterations = 10000;
    const size_t capacity = 1024;
    core::CacheOptimizedRingBuffer<int, capacity> buffer;
    
    Timer timer;
    
    // Push/Pop test
    for (size_t i = 0; i < iterations; ++i) {
        while (!buffer.push(static_cast<int>(i))) {
            std::this_thread::yield();
        }
        int value;
        while (!buffer.pop(value)) {
            std::this_thread::yield();
        }
    }
    double total_time = timer.elapsed_ms();
    
    std::cout << "CacheOptimizedRingBuffer:" << std::endl;
    std::cout << "  Push/Pop: " << std::fixed << std::setprecision(2) 
              << (2 * iterations / total_time) << " ops/ms" << std::endl;
}

// Test 6: CPU Affinity
void test_cpu_affinity() {
    std::cout << "\n=== CPU Affinity Test ===" << std::endl;
    
    int cpu_count = core::CPUAffinity::cpu_count();
    int current_cpu = core::CPUAffinity::current_cpu();
    
    std::cout << "CPU Count: " << cpu_count << std::endl;
    std::cout << "Current CPU: " << current_cpu << std::endl;
    
    // Test per-cpu storage
    core::PerCPUStorage<int, 128> storage;
    storage.for_current_cpu() = 42;
    
    std::cout << "Per-CPU Storage Test: " << storage.for_current_cpu() << std::endl;
}

// Test 7: Multi-threaded Performance
void test_multithreaded_queue() {
    std::cout << "\n=== Multi-threaded Queue Test ===" << std::endl;
    
    const size_t iterations = 1000;
    const size_t num_threads = 4;
    const size_t queue_size = 1024;
    
    core::LockFreeQueue<int, queue_size> queue;
    std::atomic<size_t> total_pushed{0};
    std::atomic<size_t> total_popped{0};
    
    Timer timer;
    
    // Producer threads
    std::vector<std::thread> producers;
    for (size_t t = 0; t < num_threads; ++t) {
        producers.emplace_back([&]() {
            for (size_t i = 0; i < iterations; ++i) {
                while (!queue.try_push(static_cast<int>(i))) {
                    std::this_thread::yield();
                }
                total_pushed++;
            }
        });
    }
    
    // Consumer threads
    std::vector<std::thread> consumers;
    for (size_t t = 0; t < num_threads; ++t) {
        consumers.emplace_back([&]() {
            int value;
            for (size_t i = 0; i < iterations; ++i) {
                while (!queue.try_pop(value)) {
                    std::this_thread::yield();
                }
                total_popped++;
            }
        });
    }
    
    // Wait for all threads
    for (auto& t : producers) t.join();
    for (auto& t : consumers) t.join();
    
    double total_time = timer.elapsed_ms();
    
    std::cout << "Multi-threaded LockFreeQueue (" << num_threads << " producers/consumers):" << std::endl;
    std::cout << "  Total Pushed: " << total_pushed << std::endl;
    std::cout << "  Total Popped: " << total_popped << std::endl;
    std::cout << "  Throughput:   " << std::fixed << std::setprecision(2) 
              << (2 * num_threads * iterations / total_time) << " ops/ms" << std::endl;
}

int main() {
    std::cout << "========================================" << std::endl;
    std::cout << "  Best Server Performance Tests" << std::endl;
    std::cout << "========================================" << std::endl;
    
    test_cpu_affinity();
    test_queue_performance();
    test_slab_allocator_performance();
    test_per_cpu_counter_performance();
    test_cache_aligned_counter_performance();
    test_ring_buffer_performance();
    test_multithreaded_queue();
    
    std::cout << "\n========================================" << std::endl;
    std::cout << "  All Tests Completed!" << std::endl;
    std::cout << "========================================" << std::endl;
    
    return 0;
}
