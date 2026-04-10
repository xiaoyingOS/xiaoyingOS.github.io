// TimerWheel - Hierarchical timer wheel for efficient timeout management
// 
// Implements an optimized timer wheel similar to Linux's timing wheel:
// - O(1) timer insertion and deletion
// - Hierarchical buckets for different time scales
// - Minimal memory overhead
// - Per-core sharding support

#ifndef BEST_SERVER_TIMER_TIMER_WHEEL_HPP
#define BEST_SERVER_TIMER_TIMER_WHEEL_HPP

#include <memory>
#include <functional>
#include <cstdint>
#include <atomic>
#include <vector>
#include <list>
#include <chrono>

namespace best_server {
namespace timer {

// Timer ID
using TimerID = uint64_t;

// Timer callback
using TimerCallback = std::function<void()>;

// Timer wheel statistics
struct TimerWheelStats {
    uint64_t timers_added{0};
    uint64_t timers_fired{0};
    uint64_t timers_cancelled{0};
    uint32_t active_timers{0};
    uint32_t bucket_count{0};
};

// Timer entry
struct TimerEntry {
    TimerID id;
    uint64_t expire_time_ms;
    TimerCallback callback;
    bool repeating;
    uint64_t interval_ms;
    
    // For list management
    std::list<TimerEntry*>::iterator list_it;
};

// Timer wheel bucket
class TimerBucket {
public:
    TimerBucket() {}
    
    void add_timer(TimerEntry* entry) {
        timers_.push_back(entry);
        entry->list_it = std::prev(timers_.end());
    }
    
    void remove_timer(TimerEntry* entry) {
        timers_.erase(entry->list_it);
    }
    
    std::list<TimerEntry*> get_and_clear() {
        std::list<TimerEntry*> result;
        result.swap(timers_);
        return result;
    }
    
    bool empty() const { return timers_.empty(); }
    size_t size() const { return timers_.size(); }
    
private:
    std::list<TimerEntry*> timers_;
};

// Hierarchical timer wheel
class TimerWheel {
public:
    static constexpr size_t DEFAULT_WHEEL_COUNT = 5;
    static constexpr size_t DEFAULT_BUCKETS_PER_WHEEL[] = {256, 64, 64, 64, 64};
    static constexpr uint64_t DEFAULT_TICK_MS = 1; // 1ms per tick
    
    TimerWheel(uint64_t tick_ms = DEFAULT_TICK_MS);
    ~TimerWheel();
    
    // Add a timer
    TimerID add_timer(uint64_t delay_ms, TimerCallback callback);
    
    // Add a repeating timer
    TimerID add_repeating_timer(uint64_t interval_ms, TimerCallback callback);
    
    // Remove a timer
    bool remove_timer(TimerID id);
    
    // Advance time
    void advance(uint64_t now_ms);
    
    // Get next expiration time
    uint64_t next_expiration() const;
    
    // Get statistics
    const TimerWheelStats& stats() const { return stats_; }
    
    // Check if empty
    bool empty() const { return stats_.active_timers == 0; }
    
    // Get current time
    uint64_t current_time() const { return current_time_ms_; }
    
private:
    // Calculate wheel and bucket for a given expiration time
    void calculate_position(uint64_t expire_time, size_t& wheel_index, size_t& bucket_index);
    
    // Cascade timers from one wheel to the next
    void cascade(size_t wheel_index);
    
    // Fire timers in a bucket
    void fire_bucket(TimerBucket& bucket);
    
    uint64_t tick_ms_;
    uint64_t current_time_ms_;
    
    std::vector<std::vector<TimerBucket>> wheels_;
    
    TimerID next_timer_id_;
    std::unordered_map<TimerID, std::unique_ptr<TimerEntry>> active_timers_;
    
    TimerWheelStats stats_;
};

// Per-core timer wheel (sharded)
class ShardedTimerWheel {
public:
    ShardedTimerWheel(int shard_id, uint64_t tick_ms = TimerWheel::DEFAULT_TICK_MS);
    ~ShardedTimerWheel();
    
    // Add a timer
    TimerID add_timer(uint64_t delay_ms, TimerCallback callback);
    
    // Add a repeating timer
    TimerID add_repeating_timer(uint64_t interval_ms, TimerCallback callback);
    
    // Remove a timer
    bool remove_timer(TimerID id);
    
    // Advance time
    void advance(uint64_t now_ms);
    
    // Get next expiration time
    uint64_t next_expiration() const;
    
    // Get statistics
    const TimerWheelStats& stats() const { return wheel_->stats(); }
    
    // Get shard ID
    int shard_id() const { return shard_id_; }
    
private:
    int shard_id_;
    std::unique_ptr<TimerWheel> wheel_;
};

} // namespace timer
} // namespace best_server

#endif // BEST_SERVER_TIMER_TIMER_WHEEL_HPP