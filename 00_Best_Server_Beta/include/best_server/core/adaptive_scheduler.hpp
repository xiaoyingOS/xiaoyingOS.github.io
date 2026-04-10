// Adaptive Scheduler - Dynamic scheduling strategy optimization
//
// Implements adaptive scheduling based on runtime metrics:
// - Performance metric tracking
// - Dynamic strategy selection
// - Workload-aware scheduling
// - Cache affinity optimization
// - Priority adjustment

#ifndef BEST_SERVER_CORE_ADAPTIVE_SCHEDULER_HPP
#define BEST_SERVER_CORE_ADAPTIVE_SCHEDULER_HPP

#include "best_server/core/scheduler.hpp"
#include <atomic>
#include <chrono>
#include <vector>
#include <unordered_map>

namespace best_server {
namespace core {

// Scheduling strategies
enum class SchedulingStrategy {
    FIFO,           // First-in-first-out
    LIFO,           // Last-in-first-out
    Priority,       // Priority-based
    WorkStealing,   // Work stealing
    AffinityAware,  // CPU affinity aware
    NUMAAware,      // NUMA aware
    Adaptive        // Automatic selection
};

// Performance metrics
struct PerformanceMetrics {
    // Task metrics
    std::atomic<uint64_t> tasks_completed{0};
    std::atomic<uint64_t> tasks_failed{0};
    std::atomic<uint64_t> tasks_stolen{0};
    
    // Latency metrics (nanoseconds)
    std::atomic<uint64_t> avg_latency_ns{0};
    std::atomic<uint64_t> p50_latency_ns{0};
    std::atomic<uint64_t> p95_latency_ns{0};
    std::atomic<uint64_t> p99_latency_ns{0};
    
    // Throughput metrics
    std::atomic<uint64_t> tasks_per_second{0};
    std::atomic<uint64_t> operations_per_second{0};
    
    // Resource metrics
    std::atomic<uint64_t> context_switches{0};
    std::atomic<uint64_t> cache_misses{0};
    std::atomic<uint64_t> lock_contentions{0};
    std::atomic<uint64_t> cpu_cycles{0};
    
    // Memory metrics
    std::atomic<uint64_t> memory_allocations{0};
    std::atomic<uint64_t> memory_deallocations{0};
    std::atomic<uint64_t> cache_line_contentions{0};
    
    // I/O metrics
    std::atomic<uint64_t> io_operations{0};
    std::atomic<uint64_t> io_bytes{0};
    std::atomic<uint64_t> io_wait_time_ns{0};
    
    void reset() {
        tasks_completed.store(0);
        tasks_failed.store(0);
        tasks_stolen.store(0);
        avg_latency_ns.store(0);
        p50_latency_ns.store(0);
        p95_latency_ns.store(0);
        p99_latency_ns.store(0);
        tasks_per_second.store(0);
        operations_per_second.store(0);
        context_switches.store(0);
        cache_misses.store(0);
        lock_contentions.store(0);
        cpu_cycles.store(0);
        memory_allocations.store(0);
        memory_deallocations.store(0);
        cache_line_contentions.store(0);
        io_operations.store(0);
        io_bytes.store(0);
        io_wait_time_ns.store(0);
    }
};

// Strategy effectiveness score
struct StrategyScore {
    SchedulingStrategy strategy;
    double score;  // 0.0 - 1.0
    uint64_t tasks_processed;
    uint64_t avg_latency_ns;
    
    StrategyScore() : strategy(SchedulingStrategy::FIFO), score(0.0), 
                     tasks_processed(0), avg_latency_ns(0) {}
};

// Adaptive scheduler
class AdaptiveScheduler : public Scheduler {
public:
    AdaptiveScheduler(size_t num_threads);
    ~AdaptiveScheduler();
    
    // Get current strategy
    SchedulingStrategy current_strategy() const;
    
    // Set strategy manually
    void set_strategy(SchedulingStrategy strategy);
    
    // Enable adaptive mode
    void enable_adaptive(bool enable);
    
    // Get performance metrics
    const PerformanceMetrics& metrics() const { return metrics_; }
    
    // Update metrics
    void update_task_latency(uint64_t latency_ns);
    void increment_tasks_completed();
    void increment_tasks_failed();
    void increment_tasks_stolen();
    void increment_context_switches();
    void increment_cache_misses();
    void increment_lock_contentions();
    
    // Evaluate and adjust strategy
    void evaluate_strategy();
    
    // Get strategy scores
    const std::vector<StrategyScore>& strategy_scores() const;
    
private:
    void collect_metrics();
    void calculate_strategy_scores();
    SchedulingStrategy select_best_strategy();
    void switch_strategy(SchedulingStrategy new_strategy);
    
    PerformanceMetrics metrics_;
    std::vector<StrategyScore> strategy_scores_;
    
    SchedulingStrategy current_strategy_;
    bool adaptive_enabled_;
    
    std::atomic<uint64_t> strategy_switch_count_{0};
    std::chrono::steady_clock::time_point last_strategy_change_;
    
    // Metric collection state
    std::vector<uint64_t> latency_samples_;
    size_t sample_count_;
    static constexpr size_t MAX_SAMPLES = 1000;
    
    // Thresholds for strategy switching
    static constexpr double MIN_IMPROVEMENT_THRESHOLD = 0.10;  // 10% improvement
    static constexpr size_t MIN_TASKS_BEFORE_EVAL = 1000;
};

// Task with priority for adaptive scheduling
class PrioritizedTask : public Task {
public:
    enum class Priority {
        Critical = 0,
        High = 1,
        Normal = 2,
        Low = 3,
        Background = 4
    };
    
    PrioritizedTask(Priority priority = Priority::Normal);
    
    Priority get_priority() const { return priority_; }
    void set_priority(Priority priority) { priority_ = priority; }
    
    // Compare priority (lower value = higher priority)
    bool operator<(const PrioritizedTask& other) const {
        return priority_ < other.priority_;
    }
    
private:
    Priority priority_;
};

// Adaptive work queue
class AdaptiveWorkQueue {
public:
    AdaptiveWorkQueue(SchedulingStrategy strategy = SchedulingStrategy::FIFO);
    
    void push(Task&& task);
    bool pop(Task& task);
    bool steal(Task& task);
    bool empty() const;
    size_t size() const;
    
    void set_strategy(SchedulingStrategy strategy);
    SchedulingStrategy get_strategy() const;
    
private:
    SchedulingStrategy strategy_;
    std::deque<Task> queue_;
    mutable std::shared_mutex mutex_;
};

} // namespace core
} // namespace best_server

#endif // BEST_SERVER_CORE_ADAPTIVE_SCHEDULER_HPP