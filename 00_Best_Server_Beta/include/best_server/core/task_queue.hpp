// TaskQueue - Lock-free multi-priority task queue
// 
// Implements an optimized lock-free task queue with:
// - Multiple priority levels
// - Work stealing support
// - Minimal memory allocation
// - Cache-friendly design
// - Lock-free MPSC queue for cross-thread communication

#ifndef BEST_SERVER_CORE_TASK_QUEUE_HPP
#define BEST_SERVER_CORE_TASK_QUEUE_HPP

#include <atomic>
#include <memory>
#include <vector>
#include <array>
#include <cstdint>
#include <thread>

#include "best_server/core/scheduler.hpp"

namespace best_server {
namespace core {

// Lock-free single-producer single-consumer queue (optimized)
template<typename T, size_t Capacity>
class SPSCQueue {
public:
    SPSCQueue() : head_(0), tail_(0) {
        buffer_.resize(Capacity + 1);
    }
    
    bool push(T&& item) {
        size_t next_tail = (tail_.load(std::memory_order_relaxed) + 1) % (Capacity + 1);
        if (next_tail == head_.load(std::memory_order_acquire)) {
            return false; // Queue full
        }
        
        buffer_[tail_.load(std::memory_order_relaxed)] = std::move(item);
        std::atomic_thread_fence(std::memory_order_release);
        tail_.store(next_tail, std::memory_order_relaxed);
        return true;
    }
    
    bool pop(T& item) {
        if (head_.load(std::memory_order_acquire) == tail_.load(std::memory_order_acquire)) {
            return false; // Queue empty
        }
        
        item = std::move(buffer_[head_.load(std::memory_order_relaxed)]);
        std::atomic_thread_fence(std::memory_order_release);
        head_.store((head_.load(std::memory_order_relaxed) + 1) % (Capacity + 1), 
                   std::memory_order_relaxed);
        return true;
    }
    
    bool empty() const {
        return head_.load(std::memory_order_acquire) == tail_.load(std::memory_order_acquire);
    }
    
    bool full() const {
        size_t next_tail = (tail_.load(std::memory_order_relaxed) + 1) % (Capacity + 1);
        return next_tail == head_.load(std::memory_order_acquire);
    }
    
    size_t size() const {
        size_t head = head_.load(std::memory_order_acquire);
        size_t tail = tail_.load(std::memory_order_acquire);
        if (tail >= head) {
            return tail - head;
        } else {
            return (Capacity + 1) - head + tail;
        }
    }
    
private:
    std::vector<T> buffer_;
    std::atomic<size_t> head_;
    std::atomic<size_t> tail_;
};

// Lock-free MPSC queue for cross-thread task submission
template<typename T>
class LockFreeMPSCQueue {
private:
    struct Node {
        std::atomic<T*> data;
        std::atomic<Node*> next;
        
        Node() : data(nullptr), next(nullptr) {}
    };
    
    alignas(64) std::atomic<Node*> head_;
    alignas(64) std::atomic<Node*> tail_;
    std::atomic<Node*> to_be_deleted_;
    
public:
    LockFreeMPSCQueue() {
        auto* dummy = new Node;
        head_.store(dummy, std::memory_order_relaxed);
        tail_.store(dummy, std::memory_order_relaxed);
        to_be_deleted_.store(nullptr, std::memory_order_relaxed);
    }
    
    ~LockFreeMPSCQueue() {
        // Clean up remaining nodes
        auto* node = head_.load(std::memory_order_relaxed);
        while (node) {
            auto* next = node->next.load(std::memory_order_relaxed);
            delete node;
            node = next;
        }
    }
    
    void push(T* item) {
        auto* node = new Node;
        node->data.store(item, std::memory_order_relaxed);
        
        auto* prev = tail_.exchange(node, std::memory_order_acq_rel);
        prev->next.store(node, std::memory_order_release);
    }
    
    T* pop() {
        auto* head = head_.load(std::memory_order_acquire);
        auto* next = head->next.load(std::memory_order_acquire);
        
        if (next == nullptr) {
            return nullptr;
        }
        
        T* data = next->data.exchange(nullptr, std::memory_order_acquire);
        
        if (head_.compare_exchange_strong(head, next, 
                                         std::memory_order_acq_rel)) {
            delete head;
        }
        
        return data;
    }
    
    bool empty() const {
        auto* head = head_.load(std::memory_order_acquire);
        return head->next.load(std::memory_order_acquire) == nullptr;
    }
    
    // Peek at the next item without removing it
    T* peek() const {
        auto* head = head_.load(std::memory_order_acquire);
        auto* next = head->next.load(std::memory_order_acquire);
        
        if (next == nullptr) {
            return nullptr;
        }
        
        return next->data.load(std::memory_order_acquire);
    }
};

// Multi-priority task queue with lock-free cross-thread submission
class TaskQueue {
public:
    static constexpr size_t QUEUE_SIZE = 4096;
    
    TaskQueue();
    ~TaskQueue();
    
    // Push a task (local thread)
    bool push(Task&& task);
    
    // Push a task from another thread (lock-free)
    bool push_cross_thread(Task* task);
    
    // Pop a task (local thread)
    bool pop(Task& task);
    
    // Steal a task (from other threads)
    bool steal(Task& task);
    
    // Check if empty
    bool empty() const;
    
    // Get queue size estimate
    size_t size_estimate() const;
    
    // Get priority-specific sizes
    size_t size(TaskPriority priority) const;
    
    // Clear all tasks
    void clear();
    
    // Get statistics
    uint64_t push_count() const { return push_count_.load(std::memory_order_relaxed); }
    uint64_t pop_count() const { return pop_count_.load(std::memory_order_relaxed); }
    uint64_t steal_count() const { return steal_count_.load(std::memory_order_relaxed); }
    
private:
    // One queue per priority level (local)
    using TaskQueueImpl = SPSCQueue<Task, QUEUE_SIZE>;
    std::array<TaskQueueImpl, 4> priority_queues_;
    
    // Lock-free MPSC queue for cross-thread submission
    LockFreeMPSCQueue<Task> cross_thread_queue_;
    
    // Statistics
    std::atomic<uint64_t> push_count_{0};
    std::atomic<uint64_t> pop_count_{0};
    std::atomic<uint64_t> steal_count_{0};
};

// Work stealing queue with cache optimization (Chase-Lev deque)
class WorkStealingQueue {
private:
    struct Slot {
        std::atomic<Task*> task;
    };
    
    alignas(64) std::unique_ptr<Slot[]> buffer_;
    size_t capacity_;
    alignas(64) std::atomic<size_t> bottom_;
    alignas(64) std::atomic<size_t> top_;
    
public:
    WorkStealingQueue() : capacity_(1024), bottom_(0), top_(0) {
        buffer_ = std::make_unique<Slot[]>(capacity_);
        for (size_t i = 0; i < capacity_; ++i) {
            buffer_[i].task.store(nullptr, std::memory_order_relaxed);
        }
    }
    
    ~WorkStealingQueue() {
        // Clean up remaining tasks
        while (!empty()) {
            Task task;
            pop(task);
        }
    }
    
    // Push task (owner only)
    bool push(Task&& task) {
        size_t b = bottom_.load(std::memory_order_relaxed);
        size_t t = top_.load(std::memory_order_acquire);
        
        if (b - t >= capacity_) {
            return false; // Queue full
        }
        
        // Store task in pre-allocated space
        Task* task_ptr = new Task(std::move(task));
        buffer_[b % capacity_].task.store(task_ptr, std::memory_order_relaxed);
        
        std::atomic_thread_fence(std::memory_order_release);
        bottom_.store(b + 1, std::memory_order_relaxed);
        return true;
    }
    
    // Pop task (owner only)
    bool pop(Task& task) {
        size_t b = bottom_.load(std::memory_order_relaxed) - 1;
        bottom_.store(b, std::memory_order_relaxed);
        
        std::atomic_thread_fence(std::memory_order_seq_cst);
        
        size_t t = top_.load(std::memory_order_relaxed);
        
        if (t <= b) {
            Task* task_ptr = buffer_[b % capacity_].task.load(std::memory_order_relaxed);
            if (t == b) {
                // Queue empty
                if (bottom_.compare_exchange_strong(b, b + 1, 
                                                  std::memory_order_seq_cst)) {
                    return false;
                }
                task_ptr = buffer_[b % capacity_].task.load(std::memory_order_relaxed);
            }
            
            if (task_ptr) {
                task = std::move(*task_ptr);
                task_ptr->~Task();
                delete task_ptr;
                buffer_[b % capacity_].task.store(nullptr, std::memory_order_relaxed);
            }
            
            bottom_.store(b + 1, std::memory_order_relaxed);
            return task_ptr != nullptr;
        }
        
        // Queue was stolen from
        bottom_.store(b + 1, std::memory_order_relaxed);
        return false;
    }
    
    // Steal task (others)
    bool steal(Task& task) {
        size_t t = top_.load(std::memory_order_acquire);
        
        std::atomic_thread_fence(std::memory_order_seq_cst);
        
        size_t b = bottom_.load(std::memory_order_acquire);
        
        if (t < b) {
            Task* task_ptr = buffer_[t % capacity_].task.load(std::memory_order_relaxed);
            
            if (!top_.compare_exchange_strong(t, t + 1, std::memory_order_seq_cst)) {
                return false;
            }
            
            if (task_ptr) {
                task = std::move(*task_ptr);
                task_ptr->~Task();
                delete task_ptr;
                buffer_[t % capacity_].task.store(nullptr, std::memory_order_relaxed);
            }
            
            return task_ptr != nullptr;
        }
        
        return false;
    }
    
    bool empty() const {
        size_t b = bottom_.load(std::memory_order_acquire);
        size_t t = top_.load(std::memory_order_acquire);
        return t >= b;
    }
    
    size_t size() const {
        size_t b = bottom_.load(std::memory_order_acquire);
        size_t t = top_.load(std::memory_order_acquire);
        return (b > t) ? (b - t) : 0;
    }
    
    // Peek at the next task without removing it (owner only)
    Task* peek() {
        size_t b = bottom_.load(std::memory_order_relaxed);
        size_t t = top_.load(std::memory_order_acquire);
        
        if (t >= b) {
            return nullptr;
        }
        
        size_t idx = (b - 1) % capacity_;
        return buffer_[idx].task.load(std::memory_order_acquire);
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

} // namespace core
} // namespace best_server

#endif // BEST_SERVER_CORE_TASK_QUEUE_HPP