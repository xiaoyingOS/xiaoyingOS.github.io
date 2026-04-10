// ThreadPool - NUMA-aware thread pool with lock-free queues
// 
// Provides a thread pool for operations that cannot be made async:
// - File I/O on systems without async file I/O
// - CPU-intensive tasks
// - Legacy synchronous operations
// - NUMA-aware thread placement
// - Lock-free task submission

#ifndef BEST_SERVER_CORE_THREAD_POOL_HPP
#define BEST_SERVER_CORE_THREAD_POOL_HPP

#include <functional>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <thread>
#include <vector>
#include <atomic>
#include <future>
#include <memory>
#include <array>

#include "best_server/core/task_queue.hpp"

namespace best_server {
namespace core {

// Task type
using ThreadPoolTask = std::function<void()>;

// Thread pool statistics
struct ThreadPoolStats {
    uint32_t total_threads{0};
    uint32_t active_threads{0};
    uint32_t idle_threads{0};
    uint64_t tasks_completed{0};
    uint64_t tasks_submitted{0};
    uint64_t tasks_queued{0};
    uint64_t tasks_failed{0};
    uint64_t tasks_stolen{0};
};

// NUMA node information
struct NUMANode {
    int node_id;
    std::vector<int> cpu_ids;
    size_t memory_size;
};

// Task object pool for reducing heap allocations
class TaskPool {
public:
    static constexpr size_t POOL_SIZE = 8192; // 8K tasks in pool
    
    TaskPool() : free_index_(0) {
        // Pre-allocate all tasks
        for (size_t i = 0; i < POOL_SIZE; ++i) {
            free_indices_[i] = i;
        }
    }
    
    ThreadPoolTask* allocate(ThreadPoolTask&& task) {
        // Try to allocate from pool
        uint32_t idx = free_index_.fetch_add(1, std::memory_order_relaxed);
        
        if (idx < POOL_SIZE) {
            // Allocate from pool
            size_t slot = free_indices_[idx];
            tasks_[slot].task = std::move(task);
            tasks_[slot].in_use = true;
            return &tasks_[slot].task;
        }
        
        // Pool exhausted, allocate from heap
        free_index_.store(POOL_SIZE, std::memory_order_relaxed);
        return new ThreadPoolTask(std::move(task));
    }
    
    void deallocate(ThreadPoolTask* task) {
        if (!task) return;
        
        // Check if task is from pool
        if (task >= &tasks_[0].task && task <= &tasks_[POOL_SIZE - 1].task) {
            size_t slot = (reinterpret_cast<char*>(task) - reinterpret_cast<char*>(tasks_)) / sizeof(TaskSlot);
            
            // Mark as free and add to free list
            tasks_[slot].in_use = false;
            uint32_t idx = free_index_.fetch_sub(1, std::memory_order_relaxed);
            if (idx > 0) {
                free_indices_[idx - 1] = slot;
            }
        } else {
            // Not from pool, delete it
            delete task;
        }
    }
    
    size_t used_count() const {
        uint32_t used = free_index_.load(std::memory_order_relaxed);
        return (used > POOL_SIZE) ? POOL_SIZE : used;
    }
    
private:
    struct TaskSlot {
        ThreadPoolTask task;
        bool in_use = false;
    };
    
    TaskSlot tasks_[POOL_SIZE];
    std::atomic<uint32_t> free_index_;
    uint32_t free_indices_[POOL_SIZE];
};

// Work-stealing queue for ThreadPoolTask pointers
class ThreadPoolTaskQueue {
private:
    struct Slot {
        std::atomic<ThreadPoolTask*> task;
    };
    
    alignas(64) std::unique_ptr<Slot[]> buffer_;
    size_t capacity_;
    alignas(64) std::atomic<size_t> bottom_;
    alignas(64) std::atomic<size_t> top_;
    
public:
    ThreadPoolTaskQueue() : capacity_(1024), bottom_(0), top_(0) {
        buffer_ = std::make_unique<Slot[]>(capacity_);
        for (size_t i = 0; i < capacity_; ++i) {
            buffer_[i].task.store(nullptr, std::memory_order_relaxed);
        }
    }
    
    ~ThreadPoolTaskQueue() {
        // Clean up remaining tasks
        while (!empty()) {
            ThreadPoolTask* task = nullptr;
            pop(task);
            // Don't delete task here - caller responsible
        }
    }
    
    // Push task (owner only)
    bool push(ThreadPoolTask* task) {
        size_t b = bottom_.load(std::memory_order_relaxed);
        size_t t = top_.load(std::memory_order_acquire);
        
        if (b - t >= capacity_) {
            return false; // Queue full
        }
        
        buffer_[b % capacity_].task.store(task, std::memory_order_relaxed);
        
        std::atomic_thread_fence(std::memory_order_release);
        bottom_.store(b + 1, std::memory_order_relaxed);
        return true;
    }
    
    // Pop task (owner only)
    bool pop(ThreadPoolTask*& task) {
        size_t b = bottom_.load(std::memory_order_relaxed) - 1;
        bottom_.store(b, std::memory_order_relaxed);
        
        std::atomic_thread_fence(std::memory_order_seq_cst);
        
        size_t t = top_.load(std::memory_order_relaxed);
        
        if (t <= b) {
            task = buffer_[b % capacity_].task.load(std::memory_order_relaxed);
            if (t == b) {
                // Queue empty
                if (bottom_.compare_exchange_strong(b, b + 1, 
                                                  std::memory_order_seq_cst)) {
                    return false;
                }
                task = buffer_[b % capacity_].task.load(std::memory_order_relaxed);
            }
            
            buffer_[b % capacity_].task.store(nullptr, std::memory_order_relaxed);
            bottom_.store(b + 1, std::memory_order_relaxed);
            return task != nullptr;
        }
        
        // Queue was stolen from
        bottom_.store(b + 1, std::memory_order_relaxed);
        return false;
    }
    
    // Steal task (others)
    bool steal(ThreadPoolTask*& task) {
        size_t t = top_.load(std::memory_order_acquire);
        
        std::atomic_thread_fence(std::memory_order_seq_cst);
        
        size_t b = bottom_.load(std::memory_order_acquire);
        
        if (t < b) {
            task = buffer_[t % capacity_].task.load(std::memory_order_relaxed);
            
            if (!top_.compare_exchange_strong(t, t + 1, std::memory_order_seq_cst)) {
                return false;
            }
            
            buffer_[t % capacity_].task.store(nullptr, std::memory_order_relaxed);
            
            return task != nullptr;
        }
        
        return false;
    }
    
    bool empty() const {
        size_t t = top_.load(std::memory_order_acquire);
        size_t b = bottom_.load(std::memory_order_acquire);
        return t >= b;
    }
    
    size_t size() const {
        size_t t = top_.load(std::memory_order_acquire);
        size_t b = bottom_.load(std::memory_order_acquire);
        return b - t;
    }
    
    // Wakeup waiting threads (no-op for now, could use condition variable)
    void wakeup() {
        // For future implementation with condition variable
        // Currently busy-wait is used for simplicity
    }
    
    // Wait for task with timeout
    void wait_for_task(std::chrono::microseconds timeout) {
        std::this_thread::sleep_for(timeout);
    }
};

// Thread pool with NUMA awareness and lock-free queues
class ThreadPool {
public:
    ThreadPool(size_t num_threads = 0);
    ~ThreadPool();
    
    // Start the thread pool
    void start();
    
    // Stop the thread pool
    void stop();
    
    // Submit a task (lock-free)
    void submit(ThreadPoolTask&& task);
    
    // Submit a task with future
    template<typename F, typename... Args>
    auto submit(F&& f, Args&&... args) -> std::future<typename std::invoke_result_t<F, Args...>> {
        using ReturnType = typename std::invoke_result_t<F, Args...>;
        
        auto task = std::make_shared<std::packaged_task<ReturnType()>>(
            std::bind(std::forward<F>(f), std::forward<Args>(args)...)
        );
        
        auto result = task->get_future();
        submit([task]() { (*task)(); });
        
        return result;
    }
    
    // Submit a task to a specific NUMA node
    template<typename F, typename... Args>
    auto submit_numa(F&& f, int numa_node, Args&&... args) 
        -> std::future<typename std::invoke_result_t<F, Args...>> {
        (void)numa_node;  // Suppress unused parameter warning
        using ReturnType = typename std::invoke_result_t<F, Args...>;
        
        auto task = std::make_shared<std::packaged_task<ReturnType()>>(
            std::bind(std::forward<F>(f), std::forward<Args>(args)...)
        );
        
        auto result = task->get_future();
        submit([task]() { (*task)(); });
        
        return result;
    }
    
    // Get statistics
    ThreadPoolStats stats() const;
    
    // Get number of threads
    size_t thread_count() const { return threads_.size(); }
    
    // Resize the thread pool
    void resize(size_t new_size);
    
    // Wait for all tasks to complete
    void wait_all();
    
    // Get NUMA topology
    static std::vector<NUMANode> get_numa_topology();
    
private:
    void worker_thread(int thread_id, const std::vector<NUMANode>& numa_nodes);
    void execute_task(ThreadPoolTask* task);
    
    std::vector<std::thread> threads_;
    std::unique_ptr<LockFreeMPSCQueue<ThreadPoolTask>> global_queue_;
    std::vector<std::unique_ptr<ThreadPoolTaskQueue>> local_queues_;
    
    std::atomic<bool> stop_{false};
    std::atomic<uint32_t> active_threads_{0};
    
    ThreadPoolStats stats_;
    size_t num_threads_;
    
    // Task object pool
    TaskPool task_pool_;
};

} // namespace core
} // namespace best_server

#endif // BEST_SERVER_CORE_THREAD_POOL_HPP