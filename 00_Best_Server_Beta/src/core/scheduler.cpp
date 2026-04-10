// Scheduler - Per-core sharding scheduler implementation

#include "best_server/core/scheduler.hpp"
#include "best_server/core/reactor.hpp"
#include "best_server/core/task_queue.hpp"
#include <algorithm>
#include <random>

namespace best_server {
namespace core {

// Utility functions
namespace utils {
    int cpu_count() {
#if BEST_SERVER_PLATFORM_LINUX
        return std::thread::hardware_concurrency();
#else
        return 4;  // Fallback
#endif
    }
    
    // Thread-local random number generator
    std::mt19937& get_random_generator() {
        static thread_local std::mt19937 gen(std::random_device{}());
        return gen;
    }
}

// Scheduler implementation
Scheduler::Scheduler(int shard_id)
    : shard_id_(shard_id)
    , reactor_(std::make_unique<Reactor>())
    , task_queue_(std::make_unique<TaskQueue>())
{
}

Scheduler::~Scheduler() {
    stop();
}

void Scheduler::start() {
    if (running_.load()) {
        return;
    }
    
    running_.store(true);
    thread_ = std::thread(&Scheduler::run, this);
    
    // Set CPU affinity
#if BEST_SERVER_PLATFORM_LINUX
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    CPU_SET(shard_id_, &cpuset);
    pthread_setaffinity_np(thread_.native_handle(), sizeof(cpu_set_t), &cpuset);
#endif
}

void Scheduler::stop() {
    if (!running_.load()) {
        return;
    }
    
    running_.store(false);
    reactor_->wake_up();
    
    if (thread_.joinable()) {
        thread_.join();
    }
}

void Scheduler::submit(Task&& task) {
    task_queue_->push(std::move(task));
    ++stats_.tasks_submitted;
    reactor_->wake_up();
}

bool Scheduler::try_steal(Task& task) {
    return task_queue_->steal(task);
}

bool Scheduler::is_idle() const {
    return task_queue_->empty() && reactor_->is_running();
}

void Scheduler::run() {
    while (running_.load()) {
        // Process tasks
        process_tasks();
        
        // Try to steal from neighbors if idle
        if (task_queue_->empty()) {
            try_steal_from_neighbors();
        }
        
        // Run reactor event loop
        reactor_->run_once(1);
    }
}

void Scheduler::process_tasks() {
    Task task;
    while (task_queue_->pop(task)) {
        task.execute();
        ++stats_.tasks_completed;
        ++stats_.context_switches;
    }
}

bool Scheduler::try_steal_from_neighbors() {
    if (neighbors_.empty()) {
        return false;
    }
    
    // Try to steal from random neighbor
    std::uniform_int_distribution<> dist(0, neighbors_.size() - 1);
    int idx = dist(utils::get_random_generator());
    
    Task task;
    if (neighbors_[idx]->try_steal(task)) {
        task.execute();
        ++stats_.tasks_stolen;
        ++stats_.tasks_completed;
        return true;
    }
    
    return false;
}

// SchedulerGroup implementation
SchedulerGroup::SchedulerGroup(int num_shards) {
    if (num_shards == 0) {
        num_shards = utils::cpu_count();
    }
    
    schedulers_.reserve(num_shards);
    for (int i = 0; i < num_shards; ++i) {
        schedulers_.push_back(std::make_unique<Scheduler>(i));
    }
    
    // Set up neighbor relationships for work stealing
    for (auto& scheduler : schedulers_) {
        for (auto& other : schedulers_) {
            if (scheduler.get() != other.get()) {
                scheduler->neighbors_.push_back(other.get());
            }
        }
    }
}

SchedulerGroup::~SchedulerGroup() {
    stop();
}

void SchedulerGroup::start() {
    if (running_.load()) {
        return;
    }
    
    running_.store(true);
    for (auto& scheduler : schedulers_) {
        scheduler->start();
    }
}

void SchedulerGroup::stop() {
    if (!running_.load()) {
        return;
    }
    
    running_.store(false);
    for (auto& scheduler : schedulers_) {
        scheduler->stop();
    }
}

void SchedulerGroup::submit(Task&& task, int preferred_shard) {
    if (preferred_shard >= 0 && preferred_shard < static_cast<int>(schedulers_.size())) {
        schedulers_[preferred_shard]->submit(std::move(task));
    } else {
        // Round-robin
        static std::atomic<uint32_t> next_shard{0};
        uint32_t shard = next_shard.fetch_add(1) % schedulers_.size();
        schedulers_[shard]->submit(std::move(task));
    }
}

void SchedulerGroup::submit_affinity(Task&& task, int cpu) {
    int shard = cpu % schedulers_.size();
    task.set_cpu_affinity(cpu);
    schedulers_[shard]->submit(std::move(task));
}

Scheduler* SchedulerGroup::get_scheduler(int shard_id) {
    if (shard_id >= 0 && shard_id < static_cast<int>(schedulers_.size())) {
        return schedulers_[shard_id].get();
    }
    return nullptr;
}

TaskStats SchedulerGroup::aggregate_stats() const {
    TaskStats total;
    for (const auto& scheduler : schedulers_) {
        const auto& stats = scheduler->stats();
        total.tasks_submitted += stats.tasks_submitted;
        total.tasks_completed += stats.tasks_completed;
        total.tasks_stolen += stats.tasks_stolen;
        total.tasks_queued += stats.tasks_queued;
        total.context_switches += stats.context_switches;
        total.idle_time_ns += stats.idle_time_ns;
    }
    return total;
}

void SchedulerGroup::wait_idle() {
    // Wait for all task queues to be empty
    bool all_idle = false;
    while (!all_idle) {
        all_idle = true;
        for (const auto& scheduler : schedulers_) {
            if (!scheduler->is_idle()) {
                all_idle = false;
                break;
            }
        }
        if (!all_idle) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    }
}

void SchedulerGroup::coordinate_work_stealing() {
    // Implement work stealing coordination logic
    // This can be called periodically to balance load
}

} // namespace core
} // namespace best_server