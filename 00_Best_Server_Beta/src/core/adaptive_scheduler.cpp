// Adaptive Scheduler implementation

#include "best_server/core/adaptive_scheduler.hpp"
#include <algorithm>
#include <cmath>

namespace best_server {
namespace core {

// AdaptiveScheduler implementation
AdaptiveScheduler::AdaptiveScheduler(size_t num_threads)
    : Scheduler(num_threads)
    , current_strategy_(SchedulingStrategy::WorkStealing)
    , adaptive_enabled_(true)
    , last_strategy_change_(std::chrono::steady_clock::now())
    , sample_count_(0)
{
    // Initialize strategy scores
    strategy_scores_.resize(7);
    strategy_scores_[0].strategy = SchedulingStrategy::FIFO;
    strategy_scores_[1].strategy = SchedulingStrategy::LIFO;
    strategy_scores_[2].strategy = SchedulingStrategy::Priority;
    strategy_scores_[3].strategy = SchedulingStrategy::WorkStealing;
    strategy_scores_[4].strategy = SchedulingStrategy::AffinityAware;
    strategy_scores_[5].strategy = SchedulingStrategy::NUMAAware;
    strategy_scores_[6].strategy = SchedulingStrategy::Adaptive;
}

AdaptiveScheduler::~AdaptiveScheduler() = default;

SchedulingStrategy AdaptiveScheduler::current_strategy() const {
    return current_strategy_;
}

void AdaptiveScheduler::set_strategy(SchedulingStrategy strategy) {
    if (current_strategy_ != strategy) {
        switch_strategy(strategy);
    }
}

void AdaptiveScheduler::enable_adaptive(bool enable) {
    adaptive_enabled_ = enable;
}

void AdaptiveScheduler::update_task_latency(uint64_t latency_ns) {
    if (sample_count_ < MAX_SAMPLES) {
        latency_samples_.push_back(latency_ns);
        sample_count_++;
    } else {
        // Replace oldest sample
        latency_samples_[sample_count_ % MAX_SAMPLES] = latency_ns;
        sample_count_++;
    }
    
    // Update avg latency
    if (sample_count_ > 0) {
        uint64_t sum = 0;
        size_t count = std::min(sample_count_, MAX_SAMPLES);
        for (size_t i = 0; i < count; ++i) {
            sum += latency_samples_[i];
        }
        metrics_.avg_latency_ns.store(sum / count);
    }
}

void AdaptiveScheduler::increment_tasks_completed() {
    metrics_.tasks_completed.fetch_add(1, std::memory_order_relaxed);
}

void AdaptiveScheduler::increment_tasks_failed() {
    metrics_.tasks_failed.fetch_add(1, std::memory_order_relaxed);
}

void AdaptiveScheduler::increment_tasks_stolen() {
    metrics_.tasks_stolen.fetch_add(1, std::memory_order_relaxed);
}

void AdaptiveScheduler::increment_context_switches() {
    metrics_.context_switches.fetch_add(1, std::memory_order_relaxed);
}

void AdaptiveScheduler::increment_cache_misses() {
    metrics_.cache_misses.fetch_add(1, std::memory_order_relaxed);
}

void AdaptiveScheduler::increment_lock_contentions() {
    metrics_.lock_contentions.fetch_add(1, std::memory_order_relaxed);
}

void AdaptiveScheduler::evaluate_strategy() {
    if (!adaptive_enabled_) {
        return;
    }
    
    uint64_t tasks_completed = metrics_.tasks_completed.load(std::memory_order_relaxed);
    
    // Only evaluate after enough tasks
    if (tasks_completed < MIN_TASKS_BEFORE_EVAL) {
        return;
    }
    
    // Check if enough time has passed since last strategy change
    auto now = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
        now - last_strategy_change_
    );
    
    if (elapsed.count() < 5) {  // At least 5 seconds between switches
        return;
    }
    
    collect_metrics();
    calculate_strategy_scores();
    
    SchedulingStrategy best_strategy = select_best_strategy();
    
    if (best_strategy != current_strategy_) {
        switch_strategy(best_strategy);
    }
}

const std::vector<StrategyScore>& AdaptiveScheduler::strategy_scores() const {
    return strategy_scores_;
}

void AdaptiveScheduler::collect_metrics() {
    // Collect all performance metrics
    // This would typically use system calls like perf counters
    
    // For now, we use the atomic metrics
    metrics_.tasks_per_second.store(
        metrics_.tasks_completed.load() / 
        std::max(1ULL, std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::steady_clock::now().time_since_epoch()
        ).count())
    );
}

void AdaptiveScheduler::calculate_strategy_scores() {
    // Calculate scores for each strategy based on current metrics
    
    uint64_t tasks_completed = metrics_.tasks_completed.load();
    uint64_t tasks_stolen = metrics_.tasks_stolen.load();
    uint64_t cache_misses = metrics_.cache_misses.load();
    uint64_t lock_contentions = metrics_.lock_contentions.load();
    uint64_t avg_latency = metrics_.avg_latency_ns.load();
    
    if (tasks_completed == 0) {
        return;
    }
    
    // Calculate steal ratio
    double steal_ratio = static_cast<double>(tasks_stolen) / tasks_completed;
    
    // Calculate cache miss ratio
    double cache_miss_ratio = static_cast<double>(cache_misses) / tasks_completed;
    
    // Calculate lock contention ratio
    double lock_contention_ratio = static_cast<double>(lock_contentions) / tasks_completed;
    
    // Score each strategy
    for (auto& score : strategy_scores_) {
        score.tasks_processed = tasks_completed;
        score.avg_latency_ns = avg_latency;
        
        switch (score.strategy) {
            case SchedulingStrategy::FIFO:
                // Good for low latency, bad for cache locality
                score.score = 0.5 + (1.0 - cache_miss_ratio) * 0.3 - lock_contention_ratio * 0.2;
                break;
                
            case SchedulingStrategy::LIFO:
                // Good for cache locality, bad for fairness
                score.score = 0.5 + (1.0 - cache_miss_ratio) * 0.4 - lock_contention_ratio * 0.3;
                break;
                
            case SchedulingStrategy::Priority:
                // Good for critical tasks, bad for throughput
                score.score = 0.5 + (1.0 - avg_latency / 1000000.0) * 0.3;
                break;
                
            case SchedulingStrategy::WorkStealing:
                // Good for load balancing, depends on steal ratio
                score.score = 0.5 + steal_ratio * 0.4 - cache_miss_ratio * 0.2;
                break;
                
            case SchedulingStrategy::AffinityAware:
                // Good for cache locality, depends on cache misses
                score.score = 0.5 + (1.0 - cache_miss_ratio) * 0.5;
                break;
                
            case SchedulingStrategy::NUMAAware:
                // Good for NUMA systems, depends on memory locality
                score.score = 0.5 + (1.0 - lock_contention_ratio) * 0.4;
                break;
                
            case SchedulingStrategy::Adaptive:
                // Always best
                score.score = 1.0;
                break;
        }
        
        // Clamp score to [0, 1]
        score.score = std::max(0.0, std::min(1.0, score.score));
    }
}

SchedulingStrategy AdaptiveScheduler::select_best_strategy() {
    // Find the strategy with the highest score
    SchedulingStrategy best = SchedulingStrategy::WorkStealing;
    double best_score = 0.0;
    
    for (const auto& score : strategy_scores_) {
        if (score.strategy != SchedulingStrategy::Adaptive && 
            score.score > best_score) {
            best = score.strategy;
            best_score = score.score;
        }
    }
    
    return best;
}

void AdaptiveScheduler::switch_strategy(SchedulingStrategy new_strategy) {
    if (current_strategy_ == new_strategy) {
        return;
    }
    
    // Check if improvement is significant enough
    auto& current_score = strategy_scores_[static_cast<size_t>(current_strategy_)];
    auto& new_score = strategy_scores_[static_cast<size_t>(new_strategy)];
    
    double improvement = new_score.score - current_score.score;
    
    if (improvement < MIN_IMPROVEMENT_THRESHOLD) {
        return;  // Not worth switching
    }
    
    // Switch strategy
    current_strategy_ = new_strategy;
    strategy_switch_count_++;
    last_strategy_change_ = std::chrono::steady_clock::now();
    
    // Reset latency samples for new strategy
    latency_samples_.clear();
    sample_count_ = 0;
}

// PrioritizedTask implementation
PrioritizedTask::PrioritizedTask(Priority priority)
    : priority_(priority)
{
}

// AdaptiveWorkQueue implementation
AdaptiveWorkQueue::AdaptiveWorkQueue(SchedulingStrategy strategy)
    : strategy_(strategy)
{
}

void AdaptiveWorkQueue::push(Task&& task) {
    std::lock_guard<std::shared_mutex> lock(mutex_);
    
    switch (strategy_) {
        case SchedulingStrategy::FIFO:
            queue_.push_back(std::move(task));
            break;
            
        case SchedulingStrategy::LIFO:
            queue_.push_front(std::move(task));
            break;
            
        case SchedulingStrategy::Priority:
            // Insert based on priority (simplified)
            queue_.push_back(std::move(task));
            std::stable_sort(queue_.begin(), queue_.end(),
                [](const Task& a, const Task& b) { return false; });  // Placeholder
            break;
            
        default:
            queue_.push_back(std::move(task));
            break;
    }
}

bool AdaptiveWorkQueue::pop(Task& task) {
    std::lock_guard<std::shared_mutex> lock(mutex_);
    
    if (queue_.empty()) {
        return false;
    }
    
    switch (strategy_) {
        case SchedulingStrategy::LIFO:
            task = std::move(queue_.front());
            queue_.pop_front();
            break;
            
        default:
            task = std::move(queue_.front());
            queue_.pop_front();
            break;
    }
    
    return true;
}

bool AdaptiveWorkQueue::steal(Task& task) {
    std::shared_lock<std::shared_mutex> lock(mutex_);
    
    if (queue_.empty()) {
        return false;
    }
    
    // Always steal from back
    task = std::move(queue_.back());
    queue_.pop_back();
    
    return true;
}

bool AdaptiveWorkQueue::empty() const {
    std::shared_lock<std::shared_mutex> lock(mutex_);
    return queue_.empty();
}

size_t AdaptiveWorkQueue::size() const {
    std::shared_lock<std::shared_mutex> lock(mutex_);
    return queue_.size();
}

void AdaptiveWorkQueue::set_strategy(SchedulingStrategy strategy) {
    std::lock_guard<std::shared_mutex> lock(mutex_);
    strategy_ = strategy;
}

SchedulingStrategy AdaptiveWorkQueue::get_strategy() const {
    return strategy_;
}

} // namespace core
} // namespace best_server