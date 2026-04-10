// Scheduler - Per-core sharding scheduler with zero-copy support
// 
// Implements a sharded scheduler similar to Seastar, with enhancements:
// - Improved work-stealing algorithm
// - Better cache locality
// - Reduced memory allocation
// - Enhanced NUMA awareness

#ifndef BEST_SERVER_CORE_SCHEDULER_HPP
#define BEST_SERVER_CORE_SCHEDULER_HPP

#include <memory>
#include <vector>
#include <atomic>
#include <functional>
#include <mutex>
#include <condition_variable>
#include <thread>
#include <queue>
#include <unordered_map>

namespace best_server {
namespace core {

// Forward declarations
class Reactor;
class TaskQueue;

// Task priority levels
enum class TaskPriority : uint8_t {
    Low = 0,
    Normal = 1,
    High = 2,
    Critical = 3
};

// Task statistics
struct TaskStats {
    uint64_t tasks_submitted{0};
    uint64_t tasks_completed{0};
    uint64_t tasks_stolen{0};
    uint64_t tasks_queued{0};
    uint64_t context_switches{0};
    uint64_t idle_time_ns{0};
};

// Task wrapper with SBO (Small Buffer Optimization)
class Task {
private:
    static constexpr size_t SBO_SIZE = 8 * sizeof(void*); // 64 bytes - full cache line, covers 95%+ lambdas
    
    // Virtual function table for type erasure
    struct VTable {
        void (*execute)(void*);
        void (*destroy)(void*);
        void (*move)(void*, void*);
    };
    
    template<typename F>
    struct VTableImpl : VTable {
        static VTableImpl instance;
        
        static void execute_impl(void* storage) {
            F* func = reinterpret_cast<F*>(storage);
            (*func)();
        }
        
        static void destroy_impl(void* storage) {
            F* func = reinterpret_cast<F*>(storage);
            func->~F();
        }
        
        static void move_impl(void* dst, void* src) {
            F* src_func = reinterpret_cast<F*>(src);
            new (dst) F(std::move(*src_func));
        }
        
        VTableImpl() : VTable{execute_impl, destroy_impl, move_impl} {}
    };
    
    // Storage - can be inline (SBO) or heap-allocated
    union Storage {
        alignas(std::max_align_t) char sbo[SBO_SIZE];
        void* heap;
        
        Storage() {}
        ~Storage() {}
    };
    
    Storage storage_;
    const VTable* vtable_;
    TaskPriority priority_;
    int cpu_affinity_;
    
public:
    Task() : vtable_(nullptr), priority_(TaskPriority::Normal), cpu_affinity_(-1) {}
    
    template<typename F>
    Task(F&& func, TaskPriority priority = TaskPriority::Normal)
        : priority_(priority)
        , cpu_affinity_(-1)
    {
        using FuncType = std::decay_t<F>;
        
        static_assert(alignof(FuncType) <= alignof(Storage), "Alignment mismatch");
        
        if constexpr (sizeof(FuncType) <= SBO_SIZE) {
            // Use Small Buffer Optimization (SBO) - no heap allocation
            new (&storage_.sbo) FuncType(std::forward<F>(func));
            vtable_ = &VTableImpl<FuncType>::instance;
        } else {
            // Large task - use heap allocation
            storage_.heap = new FuncType(std::forward<F>(func));
            vtable_ = &VTableImpl<FuncType>::instance;
        }
    }
    
    Task(Task&& other) noexcept
        : priority_(other.priority_)
        , cpu_affinity_(other.cpu_affinity_)
    {
        if (other.vtable_) {
            other.vtable_->move(&storage_, &other.storage_);
            vtable_ = other.vtable_;
            other.vtable_ = nullptr;
        }
    }
    
    Task& operator=(Task&& other) noexcept {
        if (this != &other) {
            // Destroy current
            if (vtable_) {
                vtable_->destroy(&storage_);
            }
            
            // Move from other
            priority_ = other.priority_;
            cpu_affinity_ = other.cpu_affinity_;
            vtable_ = other.vtable_;
            
            if (other.vtable_) {
                other.vtable_->move(&storage_, &other.storage_);
                other.vtable_ = nullptr;
            }
        }
        return *this;
    }
    
    // Disable copy
    Task(const Task&) = delete;
    Task& operator=(const Task&) = delete;
    
    ~Task() {
        if (vtable_) {
            vtable_->destroy(&storage_);
        }
    }
    
    void execute() {
        if (vtable_) {
            vtable_->execute(&storage_);
        }
    }
    
    TaskPriority priority() const { return priority_; }
    int cpu_affinity() const { return cpu_affinity_; }
    void set_cpu_affinity(int cpu) { cpu_affinity_ = cpu; }
};

// Per-core scheduler (shard)
class Scheduler {
public:
    Scheduler(int shard_id);
    ~Scheduler();
    
    // Start the scheduler
    void start();
    
    // Stop the scheduler
    void stop();
    
    // Submit a task to this shard
    void submit(Task&& task);
    
    // Try to steal a task from this shard
    bool try_steal(Task& task);
    
    // Get statistics
    const TaskStats& stats() const { return stats_; }
    
    // Get shard ID
    int shard_id() const { return shard_id_; }
    
    // Check if this shard is idle
    bool is_idle() const;
    
    // Get the reactor for this shard
    Reactor* reactor() { return reactor_.get(); }
    
private:
    void run();
    void process_tasks();
    bool try_steal_from_neighbors();
    
    int shard_id_;
    std::atomic<bool> running_{false};
    
    std::unique_ptr<Reactor> reactor_;
    std::unique_ptr<TaskQueue> task_queue_;
    
    std::thread thread_;
    TaskStats stats_;
    
    // Neighbor shards for work stealing
    std::vector<Scheduler*> neighbors_;
    
    friend class SchedulerGroup;
};

// Scheduler group - manages all shards
class SchedulerGroup {
public:
    SchedulerGroup(int num_shards = 0);
    ~SchedulerGroup();
    
    // Start all schedulers
    void start();
    
    // Stop all schedulers
    void stop();
    
    // Submit a task (automatic sharding)
    void submit(Task&& task, int preferred_shard = -1);
    
    // Submit a task with specific CPU affinity
    void submit_affinity(Task&& task, int cpu);
    
    // Get a specific scheduler
    Scheduler* get_scheduler(int shard_id);
    
    // Get total number of shards
    int shard_count() const { return schedulers_.size(); }
    
    // Get aggregate statistics
    TaskStats aggregate_stats() const;
    
    // Wait for all tasks to complete
    void wait_idle();
    
private:
    std::vector<std::unique_ptr<Scheduler>> schedulers_;
    std::atomic<bool> running_{false};
    
    // Work stealing coordinator
    void coordinate_work_stealing();
};

} // namespace core
} // namespace best_server

#endif // BEST_SERVER_CORE_SCHEDULER_HPP