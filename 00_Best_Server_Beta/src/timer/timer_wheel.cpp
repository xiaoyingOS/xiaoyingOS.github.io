// TimerWheel - Hierarchical timer wheel implementation

#include "best_server/timer/timer_wheel.hpp"
#include <algorithm>

namespace best_server {
namespace timer {

// TimerWheel implementation
TimerWheel::TimerWheel(uint64_t tick_ms)
    : tick_ms_(tick_ms)
    , current_time_ms_(0)
    , next_timer_id_(1) {
    
    // Initialize wheels
    for (size_t i = 0; i < DEFAULT_WHEEL_COUNT; ++i) {
        wheels_.emplace_back();
        wheels_[i].resize(DEFAULT_BUCKETS_PER_WHEEL[i]);
    }
}

TimerWheel::~TimerWheel() {
    // Clean up all active timers
    active_timers_.clear();
}

TimerID TimerWheel::add_timer(uint64_t delay_ms, TimerCallback callback) {
    return add_repeating_timer(delay_ms, std::move(callback));
}

TimerID TimerWheel::add_repeating_timer(uint64_t interval_ms, TimerCallback callback) {
    TimerID id = next_timer_id_++;
    
    auto entry = new TimerEntry();
    entry->id = id;
    entry->expire_time_ms = current_time_ms_ + interval_ms;
    entry->callback = std::move(callback);
    entry->repeating = true;
    entry->interval_ms = interval_ms;
    
    size_t wheel_idx, bucket_idx;
    calculate_position(entry->expire_time_ms, wheel_idx, bucket_idx);
    
    wheels_[wheel_idx][bucket_idx].add_timer(entry);
    active_timers_[id] = std::unique_ptr<TimerEntry>(entry);
    
    ++stats_.timers_added;
    ++stats_.active_timers;
    
    return id;
}

bool TimerWheel::remove_timer(TimerID id) {
    auto it = active_timers_.find(id);
    if (it == active_timers_.end()) {
        return false;
    }
    
    // Timer will be cleaned up when it's fired
    // For immediate removal, we'd need to search through buckets
    // For simplicity, just mark it as invalid
    it->second->callback = nullptr;
    
    ++stats_.timers_cancelled;
    --stats_.active_timers;
    
    return true;
}

void TimerWheel::advance(uint64_t now_ms) {
    if (now_ms <= current_time_ms_) {
        return;
    }
    
    uint64_t delta = now_ms - current_time_ms_;
    uint64_t ticks = delta / tick_ms_;
    
    for (uint64_t i = 0; i < ticks; ++i) {
        current_time_ms_ += tick_ms_;
        
        // Cascade through wheels
        for (size_t wheel_idx = 0; wheel_idx < wheels_.size(); ++wheel_idx) {
            size_t bucket_idx = (current_time_ms_ / tick_ms_) % wheels_[wheel_idx].size();
            
            if (bucket_idx == 0 && wheel_idx > 0) {
                cascade(wheel_idx);
            }
            
            fire_bucket(wheels_[wheel_idx][bucket_idx]);
        }
    }
}

uint64_t TimerWheel::next_expiration() const {
    uint64_t next_time = UINT64_MAX;
    
    for (const auto& pair : active_timers_) {
        if (pair.second->callback && pair.second->expire_time_ms < next_time) {
            next_time = pair.second->expire_time_ms;
        }
    }
    
    return next_time;
}

void TimerWheel::calculate_position(uint64_t expire_time, size_t& wheel_index, size_t& bucket_index) {
    uint64_t delta = expire_time - current_time_ms_;
    uint64_t ticks = delta / tick_ms_;
    
    if (ticks == 0) {
        wheel_index = 0;
        bucket_index = 1;
        return;
    }
    
    // Find the appropriate wheel
    for (size_t i = 0; i < wheels_.size(); ++i) {
        uint64_t capacity = wheels_[i].size();
        if (ticks < capacity) {
            wheel_index = i;
            bucket_index = (current_time_ms_ / tick_ms_ + ticks) % capacity;
            return;
        }
        ticks /= capacity;
    }
    
    // Too far in the future, put in last wheel
    wheel_index = wheels_.size() - 1;
    bucket_index = (current_time_ms_ / tick_ms_) % wheels_[wheel_index].size();
}

void TimerWheel::cascade(size_t wheel_index) {
    if (wheel_index == 0) {
        return;
    }
    
    size_t bucket_idx = (current_time_ms_ / tick_ms_) % wheels_[wheel_index].size();
    auto timers = wheels_[wheel_index][bucket_idx].get_and_clear();
    
    for (auto* entry : timers) {
        if (entry->expire_time_ms <= current_time_ms_) {
            // Timer expired
            if (entry->callback) {
                entry->callback();
                ++stats_.timers_fired;
            }
            
            if (entry->repeating) {
                // Reschedule repeating timer
                entry->expire_time_ms = current_time_ms_ + entry->interval_ms;
                size_t new_wheel_idx, new_bucket_idx;
                calculate_position(entry->expire_time_ms, new_wheel_idx, new_bucket_idx);
                wheels_[new_wheel_idx][new_bucket_idx].add_timer(entry);
            } else {
                // Remove single-shot timer
                active_timers_.erase(entry->id);
                --stats_.active_timers;
                delete entry;
            }
        } else {
            // Move to lower wheel
            size_t new_wheel_idx, new_bucket_idx;
            calculate_position(entry->expire_time_ms, new_wheel_idx, new_bucket_idx);
            wheels_[new_wheel_idx][new_bucket_idx].add_timer(entry);
        }
    }
}

void TimerWheel::fire_bucket(TimerBucket& bucket) {
    auto timers = bucket.get_and_clear();
    
    for (auto* entry : timers) {
        if (!entry->callback) {
            // Timer was cancelled
            active_timers_.erase(entry->id);
            --stats_.active_timers;
            delete entry;
            continue;
        }
        
        if (entry->expire_time_ms <= current_time_ms_) {
            // Timer expired
            entry->callback();
            ++stats_.timers_fired;
            
            if (entry->repeating) {
                // Reschedule repeating timer
                entry->expire_time_ms = current_time_ms_ + entry->interval_ms;
                size_t new_wheel_idx, new_bucket_idx;
                calculate_position(entry->expire_time_ms, new_wheel_idx, new_bucket_idx);
                wheels_[new_wheel_idx][new_bucket_idx].add_timer(entry);
            } else {
                // Remove single-shot timer
                active_timers_.erase(entry->id);
                --stats_.active_timers;
                delete entry;
            }
        }
    }
}

// ShardedTimerWheel implementation
ShardedTimerWheel::ShardedTimerWheel(int shard_id, uint64_t tick_ms)
    : shard_id_(shard_id)
    , wheel_(std::make_unique<TimerWheel>(tick_ms)) {
}

ShardedTimerWheel::~ShardedTimerWheel() = default;

TimerID ShardedTimerWheel::add_timer(uint64_t delay_ms, TimerCallback callback) {
    return wheel_->add_timer(delay_ms, std::move(callback));
}

TimerID ShardedTimerWheel::add_repeating_timer(uint64_t interval_ms, TimerCallback callback) {
    return wheel_->add_repeating_timer(interval_ms, std::move(callback));
}

bool ShardedTimerWheel::remove_timer(TimerID id) {
    return wheel_->remove_timer(id);
}

void ShardedTimerWheel::advance(uint64_t now_ms) {
    wheel_->advance(now_ms);
}

uint64_t ShardedTimerWheel::next_expiration() const {
    return wheel_->next_expiration();
}

} // namespace timer
} // namespace best_server