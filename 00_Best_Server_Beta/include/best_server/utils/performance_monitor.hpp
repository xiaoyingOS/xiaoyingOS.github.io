// PerformanceMonitor - Performance monitoring and statistics system
// 
// Provides comprehensive performance monitoring with:
// - Real-time metrics collection
// - Hotspot detection
// - Memory usage tracking
// - CPU utilization monitoring
// - Latency measurement
// - Throughput calculation

#ifndef BEST_SERVER_UTILS_PERFORMANCE_MONITOR_HPP
#define BEST_SERVER_UTILS_PERFORMANCE_MONITOR_HPP

#include <atomic>
#include <chrono>
#include <string>
#include <unordered_map>
#include <vector>
#include <mutex>
#include <shared_mutex>
#include <functional>

namespace best_server {
namespace utils {

// Performance metric types
enum class MetricType {
    Counter,        // Monotonically increasing counter
    Gauge,          // Point-in-time value
    Histogram,      // Distribution of values
    Timer,          // Duration measurements
    Throughput      // Operations per time unit
};

// Time series data point
struct DataPoint {
    uint64_t timestamp;
    double value;
};

// Histogram bucket
struct HistogramBucket {
    double lower_bound;
    double upper_bound;
    uint64_t count;
};

// Performance metric
class PerformanceMetric {
public:
    PerformanceMetric(const std::string& name, MetricType type);
    
    const std::string& name() const { return name_; }
    MetricType type() const { return type_; }
    
    // Counter operations (lock-free)
    void increment(double delta = 1.0);
    
    // Gauge operations (lock-free)
    void set(double value);
    void add(double delta);
    
    // Timer operations (lock-fast)
    void record(double duration_ms);
    
    // Histogram operations (lock-fast)
    void observe(double value);
    
    // Get current value (lock-free)
    double value() const;
    
    // Get statistics (lock-free)
    double min() const;
    double max() const;
    double mean() const;
    double percentile(double p) const;
    
    // Reset
    void reset();
    
private:
    std::string name_;
    MetricType type_;
    
    // All operations are lock-free for counters and gauges
    alignas(64) std::atomic<double> value_{0};
    alignas(64) std::atomic<double> min_{std::numeric_limits<double>::max()};
    alignas(64) std::atomic<double> max_{std::numeric_limits<double>::min()};
    alignas(64) std::atomic<uint64_t> count_{0};
    alignas(64) std::atomic<double> sum_{0};
    
    // Histogram data (lock-fast with atomic operations)
    struct HistogramBuckets {
        std::atomic<uint64_t> buckets[10]; // 10 percentile buckets
        std::atomic<uint64_t> overflow{0};
    };
    
    // Time series data (for timers)
    static constexpr size_t MAX_TIME_SERIES = 1000;
    std::vector<DataPoint> time_series_;
    HistogramBuckets histogram_;
    mutable std::mutex mutex_;  // Protects time_series_
};

// Latency recorder (lock-free)
class LatencyRecorder {
public:
    LatencyRecorder(const std::string& name);
    
    // Record a latency measurement (lock-free)
    void record(uint64_t latency_ns);
    
    // Get statistics (lock-free)
    double mean_latency_us() const;
    double p50_latency_us() const;
    double p95_latency_us() const;
    double p99_latency_us() const;
    double p999_latency_us() const;
    uint64_t max_latency_ns() const;
    uint64_t min_latency_ns() const;
    uint64_t total_count() const;
    
    // Reset
    void reset();
    
private:
    std::string name_;
    
    // Lock-free statistics
    alignas(64) std::atomic<uint64_t> total_count_{0};
    alignas(64) std::atomic<uint64_t> sum_ns_{0};
    alignas(64) std::atomic<uint64_t> min_ns_{UINT64_MAX};
    alignas(64) std::atomic<uint64_t> max_ns_{0};
    
    // Percentile buckets (lock-free)
    struct PercentileBuckets {
        std::atomic<uint64_t> p50_ns{0};
        std::atomic<uint64_t> p95_ns{0};
        std::atomic<uint64_t> p99_ns{0};
        std::atomic<uint64_t> p999_ns{0};
    };
    PercentileBuckets percentiles_;
    
    // Sample data for accurate percentile calculation
    static constexpr size_t MAX_SAMPLES = 10000;
    std::vector<uint64_t> samples_;
    mutable std::mutex mutex_;  // Protects samples_
};

// Throughput tracker (lock-free)
class ThroughputTracker {
public:
    ThroughputTracker(const std::string& name, uint32_t window_ms = 1000);
    
    // Record an operation (lock-free)
    void record(uint64_t count = 1);
    
    // Get current throughput (lock-free)
    double ops_per_second();
    double bytes_per_second();
    
    // Get statistics (lock-free)
    double peak_throughput() const;
    double average_throughput() const;
    
    // Reset
    void reset();
    
private:
    std::string name_;
    uint32_t window_ms_;
    
    // Lock-free sliding window
    struct WindowSlot {
        std::atomic<uint64_t> count{0};
        std::atomic<uint64_t> timestamp{0};
    };
    static constexpr size_t WINDOW_SLOTS = 10;
    std::array<WindowSlot, WINDOW_SLOTS> window_;
    std::atomic<uint32_t> current_slot_{0};
    
    // Lock-free statistics
    alignas(64) std::atomic<uint64_t> total_count_{0};
    alignas(64) std::atomic<double> peak_throughput_{0};
    alignas(64) std::atomic<uint64_t> window_start_time_{0};
    
    // Samples for tracking
    static constexpr size_t MAX_SAMPLES = 1000;
    std::vector<DataPoint> samples_;
    mutable std::mutex mutex_;  // Protects samples_
};

// Performance monitor
class PerformanceMonitor {
public:
    static PerformanceMonitor& instance();
    
    // Metric management
    PerformanceMetric* get_or_create_metric(const std::string& name, MetricType type);
    void remove_metric(const std::string& name);
    
    // Convenience methods (lock-fast)
    void increment_counter(const std::string& name, double delta = 1.0);
    void set_gauge(const std::string& name, double value);
    void record_latency(const std::string& name, uint64_t latency_ns);
    void record_throughput(const std::string& name, uint64_t count = 1);
    
    // Get all metrics
    std::vector<std::pair<std::string, PerformanceMetric*>> all_metrics() const;
    
    // System metrics
    struct SystemMetrics {
        double cpu_usage_percent;
        uint64_t memory_used_bytes;
        uint64_t memory_available_bytes;
        uint64_t disk_read_bytes;
        uint64_t disk_write_bytes;
        uint64_t network_rx_bytes;
        uint64_t network_tx_bytes;
        uint64_t context_switches;
        uint64_t page_faults;
    };
    SystemMetrics get_system_metrics() const;
    
    // Snapshot
    std::unordered_map<std::string, double> snapshot() const;
    
    // Export
    std::string export_prometheus() const;
    std::string export_json() const;
    
    // Reset all metrics
    void reset_all();
    
private:
    PerformanceMonitor();
    ~PerformanceMonitor();
    
    // Use shared_mutex for read-write lock (allows concurrent reads)
    mutable std::shared_mutex metrics_mutex_;
    std::unordered_map<std::string, std::unique_ptr<PerformanceMetric>> metrics_;
    std::unordered_map<std::string, std::unique_ptr<LatencyRecorder>> latency_recorders_;
    std::unordered_map<std::string, std::unique_ptr<ThroughputTracker>> throughput_trackers_;
};

// RAII timer for automatic latency recording
class ScopedLatencyTimer {
public:
    explicit ScopedLatencyTimer(const std::string& name);
    ~ScopedLatencyTimer();
    
private:
    std::string name_;
    std::chrono::high_resolution_clock::time_point start_time_;
};

// RAII counter for automatic operation tracking
class ScopedOperationCounter {
public:
    explicit ScopedOperationCounter(const std::string& name, int64_t count = 1);
    ~ScopedOperationCounter();
    
private:
    std::string name_;
    int64_t count_;
};

// Performance guard for monitoring critical sections
class PerformanceGuard {
public:
    PerformanceGuard(const std::string& name);
    ~PerformanceGuard();
    
    void record_success();
    void record_failure();
    
private:
    std::string name_;
    std::chrono::high_resolution_clock::time_point start_time_;
    bool succeeded_{false};
};

} // namespace utils
} // namespace best_server

#endif // BEST_SERVER_UTILS_PERFORMANCE_MONITOR_HPP