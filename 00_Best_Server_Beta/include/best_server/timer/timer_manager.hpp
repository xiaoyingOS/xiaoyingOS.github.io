// TimerManager - Global timer management with per-core sharding
// 
// Provides centralized timer management with:
// - Automatic sharding across cores
// - Load balancing
// - High-resolution timing
// - Sleep optimization

#ifndef BEST_SERVER_TIMER_TIMER_MANAGER_HPP
#define BEST_SERVER_TIMER_TIMER_MANAGER_HPP

#include <memory>
#include <functional>
#include <vector>
#include <atomic>
#include <cstdint>
#include <chrono>
#include <thread>

#include "timer/timer_wheel.hpp"

namespace best_server {
namespace timer {

// Timer manager statistics
struct TimerManagerStats {
    uint64_t total_timers_added{0};
    uint64_t total_timers_fired{0};
    uint64_t total_timers_cancelled{0};
    uint64_t total_ticks{0};
    uint32_t active_timers{0};
    uint32_t shard_count{0};
};

// Timer manager
class TimerManager {
public:
    TimerManager();
    ~TimerManager();
    
    // Start the timer manager
    void start();
    
    // Stop the timer manager
    void stop();
    
    // Add a timer
    TimerID add_timer(uint64_t delay_ms, TimerCallback callback);
    
    // Add a timer with automatic sharding
    TimerID add_timer(uint64_t delay_ms, TimerCallback callback, int preferred_shard);
    
    // Add a repeating timer
    TimerID add_repeating_timer(uint64_t interval_ms, TimerCallback callback);
    
    // Add a repeating timer with automatic sharding
    TimerID add_repeating_timer(uint64_t interval_ms, TimerCallback callback, int preferred_shard);
    
    // Remove a timer
    bool remove_timer(TimerID id);
    
    // Get global statistics
    TimerManagerStats stats() const;
    
    // Get a specific shard
    ShardedTimerWheel* get_shard(int shard_id);
    
    // Get number of shards
    int shard_count() const { return shards_.size(); }
    
    // Run tick (called by scheduler)
    void tick();
    
    // Get next expiration time across all shards
    uint64_t next_expiration() const;
    
    // Calculate optimal sleep time
    uint64_t optimal_sleep_time() const;
    
private:
    // Select shard for a timer
    int select_shard(uint64_t delay_ms, int preferred_shard);
    
    // Get current time in milliseconds
    uint64_t current_time_ms() const;
    
    std::vector<std::unique_ptr<ShardedTimerWheel>> shards_;
    std::atomic<bool> running_{false};
    
    TimerManagerStats stats_;
    
    // Timer ID generation
    std::atomic<uint64_t> next_timer_id_{1};
    
    // Track which shard owns each timer ID
    struct TimerInfo {
        int shard_id;
        bool valid;
    };
    std::unordered_map<TimerID, TimerInfo> timer_info_;
    mutable std::mutex timer_info_mutex_;
};

// Scoped timer (RAII timer that auto-cancels)
class ScopedTimer {
public:
    ScopedTimer() : manager_(nullptr), timer_id_(0) {}
    
    ScopedTimer(TimerManager* manager, TimerID id)
        : manager_(manager), timer_id_(id) {}
    
    ~ScopedTimer() {
        cancel();
    }
    
    // Disable copy
    ScopedTimer(const ScopedTimer&) = delete;
    ScopedTimer& operator=(const ScopedTimer&) = delete;
    
    // Enable move
    ScopedTimer(ScopedTimer&& other) noexcept
        : manager_(other.manager_), timer_id_(other.timer_id_) {
        other.manager_ = nullptr;
        other.timer_id_ = 0;
    }
    
    ScopedTimer& operator=(ScopedTimer&& other) noexcept {
        if (this != &other) {
            cancel();
            manager_ = other.manager_;
            timer_id_ = other.timer_id_;
            other.manager_ = nullptr;
            other.timer_id_ = 0;
        }
        return *this;
    }
    
    void cancel() {
        if (manager_ && timer_id_ != 0) {
            manager_->remove_timer(timer_id_);
            manager_ = nullptr;
            timer_id_ = 0;
        }
    }
    
    bool is_valid() const { return manager_ != nullptr && timer_id_ != 0; }
    TimerID id() const { return timer_id_; }
    
private:
    TimerManager* manager_;
    TimerID timer_id_;
};

// Timer utilities
namespace utils {

// Get number of CPU cores
inline int cpu_count() {
    return std::max(1, static_cast<int>(std::thread::hardware_concurrency()));
}

// Convert duration to milliseconds
template<typename Rep, typename Period>
uint64_t to_milliseconds(const std::chrono::duration<Rep, Period>& duration) {
    return std::chrono::duration_cast<std::chrono::milliseconds>(duration).count();
}

// Convert milliseconds to duration
template<typename Duration = std::chrono::milliseconds>
Duration from_milliseconds(uint64_t ms) {
    return std::chrono::duration_cast<Duration>(std::chrono::milliseconds(ms));
}

// Sleep until a specific time
void sleep_until(std::chrono::steady_clock::time_point time_point);

// Sleep for a duration
void sleep_for(std::chrono::milliseconds duration);

// High-resolution sleep
void precise_sleep_for(std::chrono::microseconds duration);

} // namespace utils

} // namespace timer
} // namespace best_server

#endif // BEST_SERVER_TIMER_TIMER_MANAGER_HPP