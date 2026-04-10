// Performance benchmarks for Best Server
// 
// Comprehensive benchmarks to measure performance improvements
// and compare against baseline.

#include <benchmark/benchmark.h>
#include <vector>
#include <thread>
#include <mutex>
#include <queue>
#include <atomic>
#include <chrono>
#include <random>

#include "best_server/core/lockfree_queue.hpp"
#include "best_server/memory/slab_allocator.hpp"

using namespace best_server;

// ==================== Queue Benchmarks ====================

// Baseline: std::queue with mutex
template<typename T>
class StdQueue {
public:
    void push(T&& item) {
        std::lock_guard<std::mutex> lock(mutex_);
        queue_.push(std::move(item));
    }
    
    bool pop(T& item) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (queue_.empty()) return false;
        item = std::move(queue_.front());
        queue_.pop();
        return true;
    }
    
private:
    std::queue<T> queue_;
    std::mutex mutex_;
};

// Benchmark: Queue Push/Pop
static void BM_StdQueue_PushPop(benchmark::State& state) {
    StdQueue<int> queue;
    int value = 42;
    
    for (auto _ : state) {
        queue.push(value);
        int result;
        benchmark::DoNotOptimize(queue.pop(result));
    }
    
    state.SetItemsProcessed(state.iterations());
}

BENCHMARK(BM_StdQueue_PushPop)->Threads(1)->Threads(2)->Threads(4)->Threads(8);

static void BM_LockFreeQueue_PushPop(benchmark::State& state) {
    core::LockFreeQueue<int> queue;
    int value = 42;
    
    for (auto _ : state) {
        queue.push(value);
        int result;
        benchmark::DoNotOptimize(queue.pop(result));
    }
    
    state.SetItemsProcessed(state.iterations());
}

BENCHMARK(BM_LockFreeQueue_PushPop)->Threads(1)->Threads(2)->Threads(4)->Threads(8);

static void BM_SpscQueue_PushPop(benchmark::State& state) {
    core::SpscQueue<int> queue;
    int value = 42;
    
    for (auto _ : state) {
        queue.push(value);
        int result;
        benchmark::DoNotOptimize(queue.pop(result));
    }
    
    state.SetItemsProcessed(state.iterations());
}

BENCHMARK(BM_SpscQueue_PushPop)->Threads(1);

// ==================== Memory Allocation Benchmarks ====================

static void BM_StdAllocator(benchmark::State& state) {
    const size_t size = state.range(0);
    
    for (auto _ : state) {
        void* ptr = operator new(size);
        benchmark::DoNotOptimize(ptr);
        operator delete(ptr);
    }
    
    state.SetBytesProcessed(state.iterations() * size);
}

BENCHMARK(BM_StdAllocator)->Arg(8)->Arg(16)->Arg(32)->Arg(64)->Arg(128)->Arg(256)->Arg(512)->Arg(1024);

static void BM_SlabAllocator(benchmark::State& state) {
    const size_t size = state.range(0);
    memory::SlabAllocator& allocator = memory::SlabAllocator::instance();
    
    for (auto _ : state) {
        void* ptr = allocator.allocate(size);
        benchmark::DoNotOptimize(ptr);
        allocator.deallocate(ptr, size);
    }
    
    state.SetBytesProcessed(state.iterations() * size);
}

BENCHMARK(BM_SlabAllocator)->Arg(8)->Arg(16)->Arg(32)->Arg(64)->Arg(128)->Arg(256)->Arg(512)->Arg(1024);

static void BM_ThreadLocalSlabCache(benchmark::State& state) {
    const size_t size = state.range(0);
    memory::ThreadLocalSlabCache cache;
    
    for (auto _ : state) {
        void* ptr = cache.allocate(size);
        benchmark::DoNotOptimize(ptr);
        cache.deallocate(ptr, size);
    }
    
    state.SetBytesProcessed(state.iterations() * size);
}

BENCHMARK(BM_ThreadLocalSlabCache)->Arg(8)->Arg(16)->Arg(32)->Arg(64)->Arg(128)->Arg(256)->Arg(512)->Arg(1024);

// ==================== Concurrent Access Benchmarks ====================

static void BM_ConcurrentQueue_Contended(benchmark::State& state) {
    core::LockFreeQueue<int> queue;
    const int num_threads = state.range(0);
    const int ops_per_thread = 100000;
    std::atomic<int> ready{0};
    
    auto producer = [&]() {
        ready.wait(0);
        for (int i = 0; i < ops_per_thread; ++i) {
            queue.push(i);
        }
    };
    
    auto consumer = [&]() {
        ready.wait(0);
        int value;
        int consumed = 0;
        while (consumed < ops_per_thread * (num_threads / 2)) {
            if (queue.pop(value)) {
                consumed++;
            }
        }
    };
    
    std::vector<std::thread> threads;
    for (int i = 0; i < num_threads; ++i) {
        if (i % 2 == 0) {
            threads.emplace_back(producer);
        } else {
            threads.emplace_back(consumer);
        }
    }
    
    for (auto _ : state) {
        ready = 1;
        ready.notify_all();
        
        for (auto& t : threads) {
            t.join();
        }
        
        threads.clear();
        for (int i = 0; i < num_threads; ++i) {
            if (i % 2 == 0) {
                threads.emplace_back(producer);
            } else {
                threads.emplace_back(consumer);
            }
        }
    }
    
    state.SetItemsProcessed(state.iterations() * ops_per_thread * num_threads / 2);
}

BENCHMARK(BM_ConcurrentQueue_Contended)->Arg(2)->Arg(4)->Arg(8)->Arg(16);

// ==================== Latency Benchmarks ====================

static void BM_Queue_Latency(benchmark::State& state) {
    core::LockFreeQueue<int> queue;
    
    for (auto _ : state) {
        auto start = std::chrono::high_resolution_clock::now();
        
        queue.push(42);
        int value;
        queue.pop(value);
        
        auto end = std::chrono::high_resolution_clock::now();
        auto latency = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start);
        state.SetIterationTime(latency.count());
    }
}

BENCHMARK(BM_Queue_Latency);

// ==================== Cache Performance Benchmarks ====================

struct CacheLineData {
    alignas(64) std::atomic<uint64_t> counter;
    char padding[64 - sizeof(std::atomic<uint64_t>)];
};

static void BM_CacheLine_Contention(benchmark::State& state) {
    const int num_counters = state.range(0);
    std::vector<CacheLineData> counters(num_counters);
    
    auto worker = [&](int thread_id) {
        for (auto _ : state) {
            for (int i = 0; i < num_counters; ++i) {
                counters[i].counter.fetch_add(1, std::memory_order_relaxed);
            }
        }
    };
    
    const int num_threads = state.threads();
    std::vector<std::thread> threads;
    
    for (int i = 0; i < num_threads; ++i) {
        threads.emplace_back(worker, i);
    }
    
    for (auto& t : threads) {
        t.join();
    }
    
    state.SetItemsProcessed(state.iterations() * num_counters * num_threads);
}

BENCHMARK(BM_CacheLine_Contention)->Arg(1)->Arg(2)->Arg(4)->Arg(8)->Threads(1)->Threads(2)->Threads(4)->Threads(8);

// ==================== Main ====================

BENCHMARK_MAIN();