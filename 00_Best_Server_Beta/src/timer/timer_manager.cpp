// TimerManager - Global timer management implementation

#include "best_server/timer/timer_manager.hpp"
#include <algorithm>
#include <thread>

namespace best_server {
namespace timer {

// TimerManager implementation
TimerManager::TimerManager() {
    int num_shards = utils::cpu_count();
    for (int i = 0; i < num_shards; ++i) {
        shards_.push_back(std::make_unique<ShardedTimerWheel>(i));
    }
}

TimerManager::~TimerManager() {
    stop();
}

void TimerManager::start() {
    if (running_.load()) {
        return;
    }
    
    running_.store(true);
    stats_.shard_count = shards_.size();
}

void TimerManager::stop() {
    if (!running_.load()) {
        return;
    }
    
    running_.store(false);
}

TimerID TimerManager::add_timer(uint64_t delay_ms, TimerCallback callback) {
    return add_timer(delay_ms, std::move(callback), -1);
}

TimerID TimerManager::add_timer(uint64_t delay_ms, TimerCallback callback, int preferred_shard) {
    int shard_id = select_shard(delay_ms, preferred_shard);
    auto id = shards_[shard_id]->add_timer(delay_ms, std::move(callback));
    
    {
        std::lock_guard<std::mutex> lock(timer_info_mutex_);
        TimerInfo info;
        info.shard_id = shard_id;
        info.valid = true;
        timer_info_[id] = info;
    }
    
    ++stats_.total_timers_added;
    return id;
}

TimerID TimerManager::add_repeating_timer(uint64_t interval_ms, TimerCallback callback) {
    return add_repeating_timer(interval_ms, std::move(callback), -1);
}

TimerID TimerManager::add_repeating_timer(uint64_t interval_ms, TimerCallback callback, int preferred_shard) {
    int shard_id = select_shard(interval_ms, preferred_shard);
    auto id = shards_[shard_id]->add_repeating_timer(interval_ms, std::move(callback));
    
    {
        std::lock_guard<std::mutex> lock(timer_info_mutex_);
        TimerInfo info;
        info.shard_id = shard_id;
        info.valid = true;
        timer_info_[id] = info;
    }
    
    ++stats_.total_timers_added;
    return id;
}

bool TimerManager::remove_timer(TimerID id) {
    std::lock_guard<std::mutex> lock(timer_info_mutex_);
    auto it = timer_info_.find(id);
    if (it == timer_info_.end() || !it->second.valid) {
        return false;
    }
    
    it->second.valid = false;
    bool success = shards_[it->second.shard_id]->remove_timer(id);
    
    if (success) {
        ++stats_.total_timers_cancelled;
    }
    
    return success;
}

TimerManagerStats TimerManager::stats() const {
    std::lock_guard<std::mutex> lock(timer_info_mutex_);
    TimerManagerStats s = stats_;
    
    // Aggregate shard statistics
    for (const auto& shard : shards_) {
        const auto& shard_stats = shard->stats();
        s.total_timers_fired += shard_stats.timers_fired;
        s.active_timers += shard_stats.active_timers;
    }
    
    return s;
}

ShardedTimerWheel* TimerManager::get_shard(int shard_id) {
    if (shard_id >= 0 && shard_id < static_cast<int>(shards_.size())) {
        return shards_[shard_id].get();
    }
    return nullptr;
}

void TimerManager::tick() {
    if (!running_.load()) {
        return;
    }
    
    uint64_t now = current_time_ms();
    
    for (auto& shard : shards_) {
        shard->advance(now);
    }
    
    ++stats_.total_ticks;
}

uint64_t TimerManager::next_expiration() const {
    uint64_t next = UINT64_MAX;
    
    for (const auto& shard : shards_) {
        uint64_t shard_next = shard->next_expiration();
        if (shard_next < next) {
            next = shard_next;
        }
    }
    
    return next;
}

uint64_t TimerManager::optimal_sleep_time() const {
    uint64_t next = next_expiration();
    uint64_t now = current_time_ms();
    
    if (next <= now) {
        return 0;
    }
    
    return next - now;
}

int TimerManager::select_shard(uint64_t delay_ms, int preferred_shard) {
    (void)delay_ms;
    if (preferred_shard >= 0 && preferred_shard < static_cast<int>(shards_.size())) {
        return preferred_shard;
    }
    
    // Round-robin based on delay to distribute load
    static std::atomic<uint32_t> counter{0};
    return counter.fetch_add(1) % shards_.size();
}

uint64_t TimerManager::current_time_ms() const {
#if BEST_SERVER_PLATFORM_LINUX || BEST_SERVER_PLATFORM_MACOS
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
#elif BEST_SERVER_PLATFORM_WINDOWS
    LARGE_INTEGER frequency, counter;
    QueryPerformanceFrequency(&frequency);
    QueryPerformanceCounter(&counter);
    return static_cast<uint64_t>(counter.QuadPart * 1000.0 / frequency.QuadPart);
#else
    return 0;
#endif
}

// Scoped timer implementation is header-only

// Utilities implementation
namespace utils {

uint64_t to_milliseconds(std::chrono::milliseconds duration) {
    return duration.count();
}

std::chrono::milliseconds from_milliseconds(uint64_t ms) {
    return std::chrono::milliseconds(ms);
}

void sleep_until(std::chrono::steady_clock::time_point time_point) {
    (void)time_point;
    std::this_thread::sleep_until(time_point);
}

void sleep_for(std::chrono::milliseconds duration) {
    (void)duration;
    std::this_thread::sleep_for(duration);
}

void precise_sleep_for(std::chrono::microseconds duration) {
    (void)duration;
#if BEST_SERVER_PLATFORM_LINUX
    struct timespec ts;
    ts.tv_sec = duration.count() / 1000000;
    ts.tv_nsec = (duration.count() % 1000000) * 1000;
    nanosleep(&ts, nullptr);
#else
    std::this_thread::sleep_for(duration);
#endif
}

} // namespace utils

} // namespace timer
} // namespace best_server