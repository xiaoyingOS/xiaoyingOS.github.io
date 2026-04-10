// PerformanceMonitor implementation

#include "best_server/utils/performance_monitor.hpp"
#include <algorithm>
#include <numeric>
#include <cmath>
#include <fstream>

#if BEST_SERVER_PLATFORM_LINUX
#include <sys/resource.h>
#include <unistd.h>
#include <sys/sysinfo.h>
#include <fstream>
#endif

namespace best_server {
namespace utils {

// PerformanceMetric implementation
PerformanceMetric::PerformanceMetric(const std::string& name, MetricType type)
    : name_(name)
    , type_(type) {
}

void PerformanceMetric::increment(double delta) {
    value_.fetch_add(delta, std::memory_order_relaxed);
    count_.fetch_add(1, std::memory_order_relaxed);
}

void PerformanceMetric::set(double value) {
    value_.store(value, std::memory_order_release);
    count_.fetch_add(1, std::memory_order_relaxed);
}

void PerformanceMetric::add(double delta) {
    value_.fetch_add(delta, std::memory_order_relaxed);
}

void PerformanceMetric::record(double duration_ms) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    value_.store(duration_ms, std::memory_order_release);
    sum_.fetch_add(duration_ms, std::memory_order_relaxed);
    count_.fetch_add(1, std::memory_order_relaxed);
    
    double current_min = min_.load(std::memory_order_relaxed);
    while (duration_ms < current_min && 
           !min_.compare_exchange_weak(current_min, duration_ms, 
                                       std::memory_order_release, std::memory_order_relaxed)) {
    }
    
    double current_max = max_.load(std::memory_order_relaxed);
    while (duration_ms > current_max && 
           !max_.compare_exchange_weak(current_max, duration_ms, 
                                       std::memory_order_release, std::memory_order_relaxed)) {
    }
    
    // Add to time series
    auto now = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()
    ).count();
    
    time_series_.push_back({static_cast<uint64_t>(now), duration_ms});
    if (time_series_.size() > MAX_TIME_SERIES) {
        time_series_.erase(time_series_.begin());
    }
}

void PerformanceMetric::observe(double value) {
    record(value);
}

double PerformanceMetric::value() const {
    return value_.load(std::memory_order_acquire);
}

double PerformanceMetric::min() const {
    return min_.load(std::memory_order_acquire);
}

double PerformanceMetric::max() const {
    return max_.load(std::memory_order_acquire);
}

double PerformanceMetric::mean() const {
    uint64_t count = count_.load(std::memory_order_acquire);
    if (count == 0) return 0.0;
    
    double sum = sum_.load(std::memory_order_acquire);
    return sum / count;
}

double PerformanceMetric::percentile(double p) const {
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (time_series_.empty()) {
        return 0.0;
    }
    
    std::vector<double> values;
    values.reserve(time_series_.size());
    for (const auto& point : time_series_) {
        values.push_back(point.value);
    }
    
    std::sort(values.begin(), values.end());
    
    size_t index = static_cast<size_t>(p * values.size());
    index = std::min(index, values.size() - 1);
    
    return values[index];
}

void PerformanceMetric::reset() {
    value_.store(0, std::memory_order_release);
    min_.store(std::numeric_limits<double>::max(), std::memory_order_release);
    max_.store(std::numeric_limits<double>::min(), std::memory_order_release);
    count_.store(0, std::memory_order_release);
    sum_.store(0, std::memory_order_release);
    
    std::lock_guard<std::mutex> lock(mutex_);
    time_series_.clear();
}

// LatencyRecorder implementation
LatencyRecorder::LatencyRecorder(const std::string& name)
    : name_(name) {
}

void LatencyRecorder::record(uint64_t latency_ns) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    total_count_++;
    sum_ns_ += latency_ns;
    
    if (latency_ns < min_ns_) {
        min_ns_ = latency_ns;
    }
    
    if (latency_ns > max_ns_) {
        max_ns_ = latency_ns;
    }
    
    samples_.push_back(latency_ns);
    if (samples_.size() > MAX_SAMPLES) {
        samples_.erase(samples_.begin());
    }
}

double LatencyRecorder::mean_latency_us() const {
    std::lock_guard<std::mutex> lock(mutex_);
    if (total_count_ == 0) return 0.0;
    return (sum_ns_ / total_count_) / 1000.0;
}

double LatencyRecorder::p50_latency_us() const {
    std::lock_guard<std::mutex> lock(mutex_);
    if (samples_.empty()) return 0.0;
    
    auto sorted = samples_;
    std::sort(sorted.begin(), sorted.end());
    
    size_t index = sorted.size() / 2;
    return sorted[index] / 1000.0;
}

double LatencyRecorder::p95_latency_us() const {
    std::lock_guard<std::mutex> lock(mutex_);
    if (samples_.empty()) return 0.0;
    
    auto sorted = samples_;
    std::sort(sorted.begin(), sorted.end());
    
    size_t index = static_cast<size_t>(0.95 * sorted.size());
    index = std::min(index, sorted.size() - 1);
    
    return sorted[index] / 1000.0;
}

double LatencyRecorder::p99_latency_us() const {
    std::lock_guard<std::mutex> lock(mutex_);
    if (samples_.empty()) return 0.0;
    
    auto sorted = samples_;
    std::sort(sorted.begin(), sorted.end());
    
    size_t index = static_cast<size_t>(0.99 * sorted.size());
    index = std::min(index, sorted.size() - 1);
    
    return sorted[index] / 1000.0;
}

double LatencyRecorder::p999_latency_us() const {
    std::lock_guard<std::mutex> lock(mutex_);
    if (samples_.empty()) return 0.0;
    
    auto sorted = samples_;
    std::sort(sorted.begin(), sorted.end());
    
    size_t index = static_cast<size_t>(0.999 * sorted.size());
    index = std::min(index, sorted.size() - 1);
    
    return sorted[index] / 1000.0;
}

uint64_t LatencyRecorder::max_latency_ns() const {
    return max_ns_;
}

uint64_t LatencyRecorder::min_latency_ns() const {
    return min_ns_;
}

uint64_t LatencyRecorder::total_count() const {
    return total_count_;
}

void LatencyRecorder::reset() {
    std::lock_guard<std::mutex> lock(mutex_);
    samples_.clear();
    total_count_ = 0;
    sum_ns_ = 0;
    min_ns_ = UINT64_MAX;
    max_ns_ = 0;
}

// ThroughputTracker implementation
ThroughputTracker::ThroughputTracker(const std::string& name, uint32_t window_ms)
    : name_(name)
    , window_ms_(window_ms) {
}

void ThroughputTracker::record(uint64_t count) {
    auto now = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()
    ).count();
    
    std::lock_guard<std::mutex> lock(mutex_);
    samples_.push_back({static_cast<uint64_t>(now), static_cast<double>(count)});
    total_count_ += count;
}

double ThroughputTracker::ops_per_second() {
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (samples_.empty()) {
        return 0.0;
    }
    
    auto now = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()
    ).count();
    
    uint64_t window_start = now - window_ms_;
    
    // Remove old samples
    while (!samples_.empty() && samples_.front().timestamp < window_start) {
        samples_.erase(samples_.begin());
    }
    
    if (samples_.empty()) {
        return 0.0;
    }
    
    uint64_t total = 0;
    for (const auto& sample : samples_) {
        total += sample.value;
    }
    
    return (total * 1000.0) / window_ms_;
}

double ThroughputTracker::bytes_per_second() {
    // Same as ops_per_second, just semantic difference
    return ops_per_second();
}

double ThroughputTracker::peak_throughput() const {
    return peak_throughput_.load(std::memory_order_acquire);
}

double ThroughputTracker::average_throughput() const {
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (samples_.empty()) {
        return 0.0;
    }
    
    uint64_t total = 0;
    for (const auto& sample : samples_) {
        total += sample.value;
    }
    
    return (total * 1000.0) / (samples_.back().timestamp - samples_.front().timestamp);
}

void ThroughputTracker::reset() {
    std::lock_guard<std::mutex> lock(mutex_);
    samples_.clear();
    total_count_ = 0;
    peak_throughput_ = 0;
}

// PerformanceMonitor implementation
PerformanceMonitor& PerformanceMonitor::instance() {
    static PerformanceMonitor instance;
    return instance;
}

PerformanceMonitor::PerformanceMonitor() {
}

PerformanceMonitor::~PerformanceMonitor() = default;

PerformanceMetric* PerformanceMonitor::get_or_create_metric(const std::string& name, MetricType type) {
    std::unique_lock<std::shared_mutex> lock(metrics_mutex_);
    
    auto it = metrics_.find(name);
    if (it != metrics_.end()) {
        return it->second.get();
    }
    
    auto metric = std::make_unique<PerformanceMetric>(name, type);
    auto* ptr = metric.get();
    metrics_[name] = std::move(metric);
    
    return ptr;
}

void PerformanceMonitor::remove_metric(const std::string& name) {
    std::unique_lock<std::shared_mutex> lock(metrics_mutex_);
    metrics_.erase(name);
}

void PerformanceMonitor::increment_counter(const std::string& name, double delta) {
    auto* metric = get_or_create_metric(name, MetricType::Counter);
    metric->increment(delta);
}

void PerformanceMonitor::set_gauge(const std::string& name, double value) {
    auto* metric = get_or_create_metric(name, MetricType::Gauge);
    metric->set(value);
}

void PerformanceMonitor::record_latency(const std::string& name, uint64_t latency_ns) {
    std::unique_lock<std::shared_mutex> lock(metrics_mutex_);
    
    auto it = latency_recorders_.find(name);
    if (it == latency_recorders_.end()) {
        latency_recorders_[name] = std::make_unique<LatencyRecorder>(name);
        it = latency_recorders_.find(name);
    }
    
    it->second->record(latency_ns);
}

void PerformanceMonitor::record_throughput(const std::string& name, uint64_t count) {
    std::unique_lock<std::shared_mutex> lock(metrics_mutex_);
    
    auto it = throughput_trackers_.find(name);
    if (it == throughput_trackers_.end()) {
        throughput_trackers_[name] = std::make_unique<ThroughputTracker>(name);
        it = throughput_trackers_.find(name);
    }
    
    it->second->record(count);
}

std::vector<std::pair<std::string, PerformanceMetric*>> PerformanceMonitor::all_metrics() const {
    std::unique_lock<std::shared_mutex> lock(metrics_mutex_);
    
    std::vector<std::pair<std::string, PerformanceMetric*>> result;
    for (const auto& pair : metrics_) {
        result.push_back({pair.first, pair.second.get()});
    }
    
    return result;
}

PerformanceMonitor::SystemMetrics PerformanceMonitor::get_system_metrics() const {
    SystemMetrics metrics{};
    
#if BEST_SERVER_PLATFORM_LINUX
    // CPU usage
    std::ifstream proc_stat("/proc/stat");
    if (proc_stat.is_open()) {
        std::string line;
        std::getline(proc_stat, line);
        // Parse CPU line
        // metrics.cpu_usage_percent = ...
    }
    
    // Memory
    struct sysinfo info;
    if (sysinfo(&info) == 0) {
        metrics.memory_available_bytes = info.totalram * info.mem_unit;
        // metrics.memory_used_bytes = ...
    }
    
    // I/O stats
    std::ifstream proc_diskstats("/proc/diskstats");
    if (proc_diskstats.is_open()) {
        // Parse disk stats
    }
    
    // Network stats
    std::ifstream proc_net_dev("/proc/net/dev");
    if (proc_net_dev.is_open()) {
        // Parse network stats
    }
    
    // Context switches and page faults
    struct rusage usage;
    if (getrusage(RUSAGE_SELF, &usage) == 0) {
        metrics.context_switches = usage.ru_nvcsw;
        metrics.page_faults = usage.ru_majflt;
    }
#endif
    
    return metrics;
}

std::unordered_map<std::string, double> PerformanceMonitor::snapshot() const {
    std::unordered_map<std::string, double> result;
    
    auto metrics = all_metrics();
    for (const auto& pair : metrics) {
        result[pair.first] = pair.second->value();
    }
    
    return result;
}

std::string PerformanceMonitor::export_prometheus() const {
    std::string output;
    
    auto metrics = all_metrics();
    for (const auto& pair : metrics) {
        const auto& name = pair.first;
        const auto* metric = pair.second;
        
        output += "# HELP " + name + " " + name + "\n";
        output += "# TYPE " + name + " gauge\n";
        output += name + " " + std::to_string(metric->value()) + "\n";
    }
    
    return output;
}

std::string PerformanceMonitor::export_json() const {
    // Simplified JSON export
    return "{ \"metrics\": {} }";
}

void PerformanceMonitor::reset_all() {
    std::unique_lock<std::shared_mutex> lock(metrics_mutex_);
    
    for (auto& pair : metrics_) {
        pair.second->reset();
    }
    
    for (auto& pair : latency_recorders_) {
        pair.second->reset();
    }
    
    for (auto& pair : throughput_trackers_) {
        pair.second->reset();
    }
}

// ScopedLatencyTimer implementation
ScopedLatencyTimer::ScopedLatencyTimer(const std::string& name)
    : name_(name)
    , start_time_(std::chrono::high_resolution_clock::now()) {
}

ScopedLatencyTimer::~ScopedLatencyTimer() {
    auto end_time = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::nanoseconds>(
        end_time - start_time_
    ).count();
    
    PerformanceMonitor::instance().record_latency(name_, duration);
}

// ScopedOperationCounter implementation
ScopedOperationCounter::ScopedOperationCounter(const std::string& name, int64_t count)
    : name_(name)
    , count_(count) {
    PerformanceMonitor::instance().increment_counter(name_ + "_started", count);
}

ScopedOperationCounter::~ScopedOperationCounter() {
    PerformanceMonitor::instance().increment_counter(name_ + "_completed", count_);
}

// PerformanceGuard implementation
PerformanceGuard::PerformanceGuard(const std::string& name)
    : name_(name)
    , start_time_(std::chrono::high_resolution_clock::now()) {
    PerformanceMonitor::instance().increment_counter(name_ + "_attempts");
}

PerformanceGuard::~PerformanceGuard() {
    auto end_time = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::nanoseconds>(
        end_time - start_time_
    ).count();
    
    if (succeeded_) {
        PerformanceMonitor::instance().increment_counter(name_ + "_success");
        PerformanceMonitor::instance().record_latency(name_ + "_success_latency", duration);
    } else {
        PerformanceMonitor::instance().increment_counter(name_ + "_failure");
        PerformanceMonitor::instance().record_latency(name_ + "_failure_latency", duration);
    }
}

void PerformanceGuard::record_success() {
    succeeded_ = true;
}

void PerformanceGuard::record_failure() {
    succeeded_ = false;
}

} // namespace utils
} // namespace best_server