// Performance Benchmark
// 
// Comprehensive performance benchmarks for Best_Server components

#include <best_server/best_server.hpp>
#include <best_server/utils/performance_monitor.hpp>
#include <iostream>
#include <thread>
#include <chrono>
#include <random>
#include <vector>

using namespace best_server;
using namespace best_server::utils;

// Benchmark configuration
struct BenchmarkConfig {
    size_t iterations{1000000};
    size_t threads{std::thread::hardware_concurrency()};
    size_t buffer_size{4096};
};

// Memory allocation benchmark
void benchmark_memory_allocation(const BenchmarkConfig& config) {
    std::cout << "\n=== Memory Allocation Benchmark ===" << std::endl;
    
    ScopedLatencyTimer timer("memory_allocation_total");
    
    // Standard allocation
    {
        ScopedLatencyTimer std_alloc_timer("std_alloc");
        for (size_t i = 0; i < config.iterations; ++i) {
            void* ptr = malloc(config.buffer_size);
            if (ptr) {
                memset(ptr, 0, config.buffer_size);
                free(ptr);
            }
        }
    }
    
    // Optimized pool allocation
    memory::OptimizedPool<uint8_t, config.buffer_size> pool;
    {
        ScopedLatencyTimer pool_alloc_timer("pool_alloc");
        for (size_t i = 0; i < config.iterations; ++i) {
            uint8_t* ptr = pool.allocate();
            if (ptr) {
                memset(ptr, 0, config.buffer_size);
                pool.deallocate(ptr);
            }
        }
    }
    
    auto* monitor = &PerformanceMonitor::instance();
    auto metrics = monitor->all_metrics();
    
    for (const auto& [name, metric] : metrics) {
        if (name.find("alloc") != std::string::npos) {
            std::cout << name << ": " << metric->mean() << " ms" << std::endl;
        }
    }
}

// Task scheduling benchmark
void benchmark_task_scheduling(const BenchmarkConfig& config) {
    std::cout << "\n=== Task Scheduling Benchmark ===" << std::endl;
    
    core::SchedulerGroup group(config.threads);
    group.start();
    
    std::atomic<uint64_t> counter{0};
    
    {
        ScopedLatencyTimer timer("task_scheduling");
        
        for (size_t i = 0; i < config.iterations; ++i) {
            group.submit(core::Task([&counter]() {
                counter.fetch_add(1, std::memory_order_relaxed);
            }));
        }
    }
    
    group.wait_idle();
    group.stop();
    
    std::cout << "Tasks completed: " << counter.load() << std::endl;
    std::cout << "Tasks per second: " << 
        (config.iterations / (PerformanceMonitor::instance().get_or_create_metric("task_scheduling", MetricType::Timer)->mean() / 1000.0)) 
        << std::endl;
}

// Future/Promise benchmark
void benchmark_future_promise(const BenchmarkConfig& config) {
    std::cout << "\n=== Future/Promise Benchmark ===" << std::endl;
    
    {
        ScopedLatencyTimer timer("future_promise_creation");
        
        for (size_t i = 0; i < config.iterations; ++i) {
            future::Promise<int> promise;
            auto future = promise.get_future();
            promise.set_value(i);
        }
    }
    
    {
        ScopedLatencyTimer timer("future_promise_chain");
        
        for (size_t i = 0; i < config.iterations; ++i) {
            future::Promise<int> promise;
            auto future = promise.get_future();
            
            auto result = future.then([](int value) {
                return value * 2;
            }).then([](int value) {
                return value + 1;
            });
            
            promise.set_value(i);
        }
    }
}

// Zero-copy buffer benchmark
void benchmark_zero_copy_buffer(const BenchmarkConfig& config) {
    std::cout << "\n=== Zero-Copy Buffer Benchmark ===" << std::endl;
    
    std::vector<uint8_t> source_data(config.buffer_size);
    std::vector<uint8_t> dest_data(config.buffer_size);
    
    // Copy with memcpy
    {
        ScopedLatencyTimer timer("memcpy");
        for (size_t i = 0; i < config.iterations; ++i) {
            memcpy(dest_data.data(), source_data.data(), config.buffer_size);
        }
    }
    
    // Copy with zero-copy buffer
    {
        ScopedLatencyTimer timer("zero_copy_buffer");
        for (size_t i = 0; i < config.iterations; ++i) {
            memory::ZeroCopyBuffer buffer(config.buffer_size);
            buffer.write(source_data.data(), config.buffer_size);
            buffer.read(dest_data.data(), config.buffer_size);
        }
    }
    
    // Zero-copy slice
    {
        ScopedLatencyTimer timer("zero_copy_slice");
        memory::ZeroCopyBuffer buffer(config.buffer_size * 2);
        buffer.write(source_data.data(), config.buffer_size);
        buffer.write(source_data.data(), config.buffer_size);
        
        for (size_t i = 0; i < config.iterations; ++i) {
            auto slice = buffer.slice(0, config.buffer_size);
            slice.read(dest_data.data(), config.buffer_size);
        }
    }
}

// Timer benchmark
void benchmark_timer(const BenchmarkConfig& config) {
    std::cout << "\n=== Timer Benchmark ===" << std::endl;
    
    timer::TimerManager timer_manager;
    timer_manager.start();
    
    std::atomic<uint64_t> timer_fired{0};
    
    {
        ScopedLatencyTimer timer("timer_operations");
        
        for (size_t i = 0; i < config.iterations; ++i) {
            timer_manager.add_timer(1, [&timer_fired]() {
                timer_fired.fetch_add(1, std::memory_order_relaxed);
            });
        }
    }
    
    // Wait for timers
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    
    timer_manager.stop();
    
    std::cout << "Timers fired: " << timer_fired.load() << std::endl;
}

// Concurrent access benchmark
void benchmark_concurrent_access(const BenchmarkConfig& config) {
    std::cout << "\n=== Concurrent Access Benchmark ===" << std::endl;
    
    std::vector<std::thread> threads;
    std::atomic<uint64_t> counter{0};
    
    {
        ScopedLatencyTimer timer("concurrent_atomic");
        
        for (size_t t = 0; t < config.threads; ++t) {
            threads.emplace_back([&counter, &config]() {
                for (size_t i = 0; i < config.iterations / config.threads; ++i) {
                    counter.fetch_add(1, std::memory_order_relaxed);
                }
            });
        }
        
        for (auto& thread : threads) {
            thread.join();
        }
    }
    
    std::cout << "Final counter: " << counter.load() << std::endl;
    std::cout << "Operations per second: " << 
        (counter.load() / (PerformanceMonitor::instance().get_or_create_metric("concurrent_atomic", MetricType::Timer)->mean() / 1000.0)) 
        << std::endl;
}

// Memory bandwidth benchmark
void benchmark_memory_bandwidth(const BenchmarkConfig& config) {
    std::cout << "\n=== Memory Bandwidth Benchmark ===" << std::endl;
    
    size_t data_size = 100 * 1024 * 1024; // 100MB
    auto* data = new uint8_t[data_size];
    
    // Sequential read
    {
        ScopedLatencyTimer timer("sequential_read");
        uint64_t sum = 0;
        for (size_t i = 0; i < data_size; ++i) {
            sum += data[i];
        }
    }
    
    // Sequential write
    {
        ScopedLatencyTimer timer("sequential_write");
        for (size_t i = 0; i < data_size; ++i) {
            data[i] = static_cast<uint8_t>(i % 256);
        }
    }
    
    // Random read
    {
        ScopedLatencyTimer timer("random_read");
        std::mt19937 rng(42);
        std::uniform_int_distribution<size_t> dist(0, data_size - 1);
        uint64_t sum = 0;
        for (size_t i = 0; i < data_size / 100; ++i) {
            sum += data[dist(rng)];
        }
    }
    
    delete[] data;
    
    auto bandwidth = (data_size / 1024.0 / 1024.0) / 
        (PerformanceMonitor::instance().get_or_create_metric("sequential_read", MetricType::Timer)->mean() / 1000.0);
    
    std::cout << "Sequential read bandwidth: " << bandwidth << " MB/s" << std::endl;
}

// Main benchmark runner
int main(int argc, char* argv[]) {
    std::cout << "Best_Server Performance Benchmark" << std::endl;
    std::cout << "=================================" << std::endl;
    
    BenchmarkConfig config;
    
    // Parse command line arguments
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--iterations" && i + 1 < argc) {
            config.iterations = std::stoull(argv[++i]);
        } else if (arg == "--threads" && i + 1 < argc) {
            config.threads = std::stoull(argv[++i]);
        } else if (arg == "--buffer-size" && i + 1 < argc) {
            config.buffer_size = std::stoull(argv[++i]);
        }
    }
    
    std::cout << "Configuration:" << std::endl;
    std::cout << "  Iterations: " << config.iterations << std::endl;
    std::cout << "  Threads: " << config.threads << std::endl;
    std::cout << "  Buffer size: " << config.buffer_size << " bytes" << std::endl;
    
    // Run benchmarks
    benchmark_memory_allocation(config);
    benchmark_task_scheduling(config);
    benchmark_future_promise(config);
    benchmark_zero_copy_buffer(config);
    benchmark_timer(config);
    benchmark_concurrent_access(config);
    benchmark_memory_bandwidth(config);
    
    // Print summary
    std::cout << "\n=== Performance Summary ===" << std::endl;
    
    auto* monitor = &PerformanceMonitor::instance();
    auto metrics = monitor->all_metrics();
    
    std::cout << "\nAll Metrics:" << std::endl;
    for (const auto& [name, metric] : metrics) {
        std::cout << "  " << name << ": " << metric->mean() << " ms";
        std::cout << " (min: " << metric->min() << ", max: " << metric->max() << ")" << std::endl;
    }
    
    // Export metrics
    std::cout << "\nPrometheus Export:" << std::endl;
    std::cout << monitor->export_prometheus() << std::endl;
    
    return 0;
}
