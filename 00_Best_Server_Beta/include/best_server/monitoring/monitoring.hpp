// Monitoring - Metrics, Tracing, and Logging integration
// 
// Provides comprehensive monitoring with:
// - Metrics collection (counters, gauges, histograms)
// - Distributed tracing
// - Performance monitoring
// - Alerting

#ifndef BEST_SERVER_MONITORING_MONITORING_HPP
#define BEST_SERVER_MONITORING_MONITORING_HPP

#include <string>
#include <vector>
#include <memory>
#include <unordered_map>
#include <functional>
#include <chrono>
#include <mutex>
#include <atomic>
#include <variant>

namespace best_server {
namespace monitoring {

// Metric types
enum class MetricType {
    COUNTER,
    GAUGE,
    HISTOGRAM,
    SUMMARY
};

// Metric value
using MetricValue = std::variant<int64_t, double>;

// Metric label
struct Label {
    std::string key;
    std::string value;
};

// Metric
class Metric {
public:
    using Ptr = std::shared_ptr<Metric>;
    
    Metric(const std::string& name, MetricType type, const std::string& description = "");
    virtual ~Metric() = default;
    
    const std::string& name() const { return name_; }
    MetricType type() const { return type_; }
    const std::string& description() const { return description_; }
    
    virtual void increment(double value = 1.0) {}
    virtual void decrement(double value = 1.0) {}
    virtual void set(double value) {}
    virtual void observe(double value) {}
    virtual void reset() {}
    
    virtual MetricValue get() const { return MetricValue{0.0}; }
    
    const std::vector<Label>& labels() const { return labels_; }
    void add_label(const Label& label);
    
protected:
    std::string name_;
    MetricType type_;
    std::string description_;
    std::vector<Label> labels_;
    mutable std::mutex mutex_;
};

// Counter
class Counter : public Metric {
public:
    using Ptr = std::shared_ptr<Counter>;
    
    static Ptr create(const std::string& name, const std::string& description = "");
    
    void increment(double value = 1.0) override;
    void reset() override;
    
    MetricValue get() const override;
    
    // Delete copy and move constructors
    Counter(const Counter&) = delete;
    Counter& operator=(const Counter&) = delete;
    Counter(Counter&&) = delete;
    Counter& operator=(Counter&&) = delete;
    
private:
    Counter(const std::string& name, const std::string& description);
    
    std::atomic<double> value_{0.0};
};

// Gauge
class Gauge : public Metric {
public:
    using Ptr = std::shared_ptr<Gauge>;
    
    static Ptr create(const std::string& name, const std::string& description = "");
    
    void increment(double value = 1.0) override;
    void decrement(double value = 1.0) override;
    void set(double value) override;
    
    MetricValue get() const override;
    
    // Delete copy and move constructors
    Gauge(const Gauge&) = delete;
    Gauge& operator=(const Gauge&) = delete;
    Gauge(Gauge&&) = delete;
    Gauge& operator=(Gauge&&) = delete;
    
private:
    Gauge(const std::string& name, const std::string& description);
    
    std::atomic<double> value_{0.0};
};

// Histogram
class Histogram : public Metric {
public:
    using Ptr = std::shared_ptr<Histogram>;
    
    static Ptr create(const std::string& name, const std::vector<double>& buckets, const std::string& description = "");
    
    void observe(double value) override;
    void reset() override;
    
    MetricValue get() const override;
    
    std::unordered_map<double, uint64_t> buckets() const;
    uint64_t count() const;
    double sum() const;
    
    // Delete copy and move constructors
    Histogram(const Histogram&) = delete;
    Histogram& operator=(const Histogram&) = delete;
    Histogram(Histogram&&) = delete;
    Histogram& operator=(Histogram&&) = delete;
    
private:
    Histogram(const std::string& name, const std::vector<double>& buckets, const std::string& description);
    
    std::vector<double> bucket_boundaries_;
    std::deque<std::atomic<uint64_t>> bucket_counts_;
    std::atomic<uint64_t> count_{0};
    std::atomic<double> sum_{0.0};
};

// Metrics registry
class MetricsRegistry {
public:
    using Ptr = std::shared_ptr<MetricsRegistry>;
    
    static Ptr create();
    
    void register_metric(Metric::Ptr metric);
    void unregister_metric(const std::string& name);
    
    Metric::Ptr get_metric(const std::string& name);
    std::vector<Metric::Ptr> get_all_metrics();
    
    // Convenience methods
    Counter::Ptr counter(const std::string& name, const std::string& description = "");
    Gauge::Ptr gauge(const std::string& name, const std::string& description = "");
    Histogram::Ptr histogram(const std::string& name, const std::vector<double>& buckets, const std::string& description = "");
    
    // Export metrics (Prometheus format)
    std::string export_prometheus();
    
private:
    MetricsRegistry();
    
    std::unordered_map<std::string, Metric::Ptr> metrics_;
    std::mutex mutex_;
};

// Span for distributed tracing
class Span {
public:
    using Ptr = std::shared_ptr<Span>;
    
    Span(const std::string& name, const std::string& trace_id, const std::string& parent_span_id = "");
    ~Span();
    
    const std::string& trace_id() const { return trace_id_; }
    const std::string& span_id() const { return span_id_; }
    const std::string& parent_span_id() const { return parent_span_id_; }
    const std::string& name() const { return name_; }
    
    void set_tag(const std::string& key, const std::string& value);
    void set_status(const std::string& status);
    void log(const std::string& message);
    
    void start();
    void finish();
    
    std::chrono::microseconds duration() const;
    
private:
    std::string trace_id_;
    std::string span_id_;
    std::string parent_span_id_;
    std::string name_;
    
    std::chrono::system_clock::time_point start_time_;
    std::chrono::system_clock::time_point end_time_;
    
    std::unordered_map<std::string, std::string> tags_;
    std::string status_;
    std::vector<std::string> logs_;
    
    static std::string generate_id();
};

// Tracer
class Tracer {
public:
    using Ptr = std::shared_ptr<Tracer>;
    
    static Ptr create(const std::string& service_name);
    
    Span::Ptr start_span(const std::string& name, Span::Ptr parent = nullptr);
    
    const std::string& service_name() const { return service_name_; }
    
private:
    Tracer(const std::string& service_name);
    
    std::string service_name_;
    std::string current_trace_id_;
};

// Performance monitor
class PerformanceMonitor {
public:
    using Ptr = std::shared_ptr<PerformanceMonitor>;
    
    static Ptr create();
    
    void record_operation(const std::string& operation, std::chrono::microseconds duration);
    
    std::unordered_map<std::string, std::vector<std::chrono::microseconds>> get_operations();
    std::chrono::microseconds get_average_duration(const std::string& operation);
    std::chrono::microseconds get_p95_duration(const std::string& operation);
    std::chrono::microseconds get_p99_duration(const std::string& operation);
    
    void reset();
    
private:
    PerformanceMonitor();
    
    std::unordered_map<std::string, std::vector<std::chrono::microseconds>> operations_;
    std::mutex mutex_;
};

// Alert
class Alert {
public:
    using Ptr = std::shared_ptr<Alert>;
    using Callback = std::function<void(const Alert&)>;
    
    Alert(const std::string& name, const std::string& description);
    
    const std::string& name() const { return name_; }
    const std::string& description() const { return description_; }
    bool is_active() const { return active_; }
    
    void trigger();
    void resolve();
    
    void set_callback(Callback callback);
    
private:
    std::string name_;
    std::string description_;
    bool active_{false};
    Callback callback_;
};

// Alert manager
class AlertManager {
public:
    using Ptr = std::shared_ptr<AlertManager>;
    
    static Ptr create();
    
    void register_alert(Alert::Ptr alert);
    void unregister_alert(const std::string& name);
    
    Alert::Ptr get_alert(const std::string& name);
    std::vector<Alert::Ptr> get_active_alerts();
    
private:
    AlertManager();
    
    std::unordered_map<std::string, Alert::Ptr> alerts_;
    std::mutex mutex_;
};

// Global monitoring instance
class Monitoring {
public:
    static MetricsRegistry::Ptr metrics();
    static Tracer::Ptr tracer();
    static PerformanceMonitor::Ptr performance();
    static AlertManager::Ptr alerts();
    
    static void initialize(const std::string& service_name);
    
private:
    static std::shared_ptr<MetricsRegistry> metrics_;
    static std::shared_ptr<Tracer> tracer_;
    static std::shared_ptr<PerformanceMonitor> performance_;
    static std::shared_ptr<AlertManager> alerts_;
};

// Convenience macros
#define METRIC_COUNTER(name) best_server::monitoring::Monitoring::metrics()->counter(name)
#define METRIC_GAUGE(name) best_server::monitoring::Monitoring::metrics()->gauge(name)
#define METRIC_HISTOGRAM(name, buckets) best_server::monitoring::Monitoring::metrics()->histogram(name, buckets)

#define TRACE_START(name) best_server::monitoring::Monitoring::tracer()->start_span(name)
#define TRACE_SCOPE(name) auto _span = best_server::monitoring::Monitoring::tracer()->start_span(name); _span->start(); _span->finish()

} // namespace monitoring
} // namespace best_server

#endif // BEST_SERVER_MONITORING_MONITORING_HPP