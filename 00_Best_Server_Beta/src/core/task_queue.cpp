// TaskQueue - Lock-free multi-priority task queue implementation

#include "best_server/core/task_queue.hpp"

namespace best_server {
namespace core {

// TaskQueue implementation
TaskQueue::TaskQueue() {
}

TaskQueue::~TaskQueue() {
    // Clean up cross-thread queue
    Task* task;
    while ((task = cross_thread_queue_.pop()) != nullptr) {
        delete task;
    }
}

bool TaskQueue::push(Task&& task) {
    int priority_idx = static_cast<int>(task.priority());
    if (priority_idx >= 0 && priority_idx < 4) {
        bool success = priority_queues_[priority_idx].push(std::move(task));
        if (success) {
            ++push_count_;
        }
        return success;
    }
    return false;
}

bool TaskQueue::push_cross_thread(Task* task) {
    if (!task) {
        return false;
    }
    
    cross_thread_queue_.push(task);
    ++push_count_;
    return true;
}

bool TaskQueue::pop(Task& task) {
    // First try local priority queues
    for (int i = 3; i >= 0; --i) {
        if (priority_queues_[i].pop(task)) {
            ++pop_count_;
            return true;
        }
    }
    
    // Then try cross-thread queue
    Task* task_ptr = cross_thread_queue_.pop();
    if (task_ptr) {
        task = std::move(*task_ptr);
        delete task_ptr;
        ++pop_count_;
        return true;
    }
    
    return false;
}

bool TaskQueue::steal(Task& task) {
    // Steal from cross-thread queue first (less contention)
    Task* task_ptr = cross_thread_queue_.pop();
    if (task_ptr) {
        task = std::move(*task_ptr);
        delete task_ptr;
        ++steal_count_;
        return true;
    }
    
    // Then steal from lowest priority first to avoid contention
    for (int i = 0; i < 4; ++i) {
        if (priority_queues_[i].pop(task)) {
            ++steal_count_;
            return true;
        }
    }
    
    return false;
}

bool TaskQueue::empty() const {
    // Check local queues from highest to lowest priority (most likely non-empty first)
    for (int i = 3; i >= 0; --i) {
        if (!priority_queues_[i].empty()) {
            return false;
        }
    }
    
    // Check cross-thread queue (less likely)
    if (!cross_thread_queue_.empty()) {
        return false;
    }
    
    return true;
}

size_t TaskQueue::size_estimate() const {
    size_t total = 0;
    for (int i = 0; i < 4; ++i) {
        total += priority_queues_[i].size();
    }
    return total;
}

size_t TaskQueue::size(TaskPriority priority) const {
    int idx = static_cast<int>(priority);
    if (idx >= 0 && idx < 4) {
        return priority_queues_[idx].size();
    }
    return 0;
}

void TaskQueue::clear() {
    for (int i = 0; i < 4; ++i) {
        Task task;
        while (priority_queues_[i].pop(task)) {
            // Task will be destroyed
        }
    }
    
    // Clear cross-thread queue
    Task* task_ptr;
    while ((task_ptr = cross_thread_queue_.pop()) != nullptr) {
        delete task_ptr;
    }
}

} // namespace core
} // namespace best_server