// RCU implementation
#include "best_server/core/rcu.hpp"
#include <algorithm>

namespace best_server {
namespace core {

// ==================== RCUManager Implementation ====================

RCUManager::RCUManager() : global_epoch_(0) {
    // 启动回收线程
    reclaimer_thread_ = std::thread(&RCUManager::reclaimer_thread, this);
}

RCUManager::~RCUManager() {
    running_.store(false, std::memory_order_release);
    reclaim_cv_.notify_all();
    
    if (reclaimer_thread_.joinable()) {
        reclaimer_thread_.join();
    }
    
    // 回收所有剩余的资源
    reclaim();
}

RCUState& RCUManager::get_thread_state() {
    std::unique_lock<std::mutex> lock(const_cast<std::mutex&>(states_mutex_));
    
    auto thread_id = std::this_thread::get_id();
    auto it = thread_states_.find(thread_id);
    
    if (it == thread_states_.end()) {
        auto state = std::make_unique<RCUState>();
        it = thread_states_.emplace(thread_id, std::move(state)).first;
    }
    
    return *it->second;
}

std::unique_ptr<RCUReadLock> RCUManager::read_lock() {
    return std::make_unique<RCUReadLock>(global_epoch_, get_thread_state());
}

void RCUManager::defer(RCUCallback callback) {
    {
        std::unique_lock<std::mutex> lock(const_cast<std::mutex&>(deferred_mutex_));
        
        uint64_t current_epoch = global_epoch_.load(std::memory_order_acquire);
        deferred_list_.emplace_back(std::move(callback), current_epoch);
        
        ++stats_total_deferred_;
    }
    
    // 通知回收线程
    reclaim_cv_.notify_one();
}

void RCUManager::update_epoch() {
    global_epoch_.fetch_add(1, std::memory_order_acq_rel);
    ++stats_epoch_updates_;
}

void RCUManager::reclaim() {
    std::unique_lock<std::mutex> lock(const_cast<std::mutex&>(deferred_mutex_));
    
    
    // 移除可以回收的节点
    auto new_end = std::remove_if(deferred_list_.begin(), deferred_list_.end(),
        [this](const RCUNode& node) {
            if (can_reclaim(node.retire_epoch)) {
                node.callback();
                ++stats_total_reclaimed_;
                return true;
            }
            return false;
        });
    
    deferred_list_.erase(new_end, deferred_list_.end());
}

bool RCUManager::can_reclaim(uint64_t retire_epoch) const {
    // 检查所有线程是否已经退出该epoch
    std::unique_lock<std::mutex> lock(const_cast<std::mutex&>(states_mutex_));
    
    for (const auto& pair : thread_states_) {
        const auto& state = pair.second;
        
        // 如果线程在读临界区且epoch小于等于retire_epoch，则不能回收
        if (state->in_read_section.load(std::memory_order_acquire)) {
            uint64_t read_epoch = global_epoch_.load(std::memory_order_acquire);
            if (read_epoch <= retire_epoch) {
                return false;
            }
        }
    }
    
    // 等待一个epoch的宽限期
    uint64_t current_epoch = global_epoch_.load(std::memory_order_acquire);
    return (current_epoch - retire_epoch) >= 1;
}

void RCUManager::reclaimer_thread() {
    while (running_.load(std::memory_order_acquire)) {
        // 等待或定期回收
        {
            std::unique_lock<std::mutex> lock(reclaim_mutex_);
            reclaim_cv_.wait_for(lock, std::chrono::milliseconds(100));
        }
        
        reclaim();
    }
}

RCUManager::Stats RCUManager::stats() const {
    Stats s;
    s.total_deferred = stats_total_deferred_.load(std::memory_order_relaxed);
    s.total_reclaimed = stats_total_reclaimed_.load(std::memory_order_relaxed);
    s.epoch_updates = stats_epoch_updates_.load(std::memory_order_relaxed);
    
    {
        std::unique_lock<std::mutex> lock(const_cast<std::mutex&>(deferred_mutex_));
        s.current_pending = deferred_list_.size();
    }
    
    return s;
}

// ==================== Global RCU Manager ====================

namespace {
    // 全局RCU管理器实例
    std::unique_ptr<RCUManager> global_rcu_instance;
    std::mutex global_rcu_mutex;
}

RCUManager& get_global_rcu() {
    std::lock_guard<std::mutex> lock(global_rcu_mutex);
    
    if (!global_rcu_instance) {
        global_rcu_instance = std::make_unique<RCUManager>();
    }
    
    return *global_rcu_instance;
}

} // namespace core
} // namespace best_server