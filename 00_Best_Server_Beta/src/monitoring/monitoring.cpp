// Monitoring implementation
#include "best_server/monitoring/monitoring.hpp"
#include <sstream>
#include <algorithm>
#include <random>
#include <iomanip>

namespace best_server {
namespace monitoring {

// Metric implementation
Metric::Metric(const std::string& name, MetricType type, const std::string& description)
    : name_(name)
    , type_(type)
    , description_(description)
{
}

void Metric::add_label(const Label& label) {
    std::lock_guard<std::mutex> lock(mutex_);
    labels_.push_back(label);
}

// Counter implementation
Counter::Counter(const std::string& name, const std::string& description)
    : Metric(name, MetricType::COUNTER, description)
{
}

Counter::Ptr Counter::create(const std::string& name, const std::string& description) {
    return std::shared_ptr<Counter>(new Counter(name, description));
}

void Counter::increment(double value) {
    value_.fetch_add(value, std::memory_order_relaxed);
}

void Counter::reset() {
    value_.store(0.0, std::memory_order_relaxed);
}

MetricValue Counter::get() const {
    return MetricValue{value_.load(std::memory_order_relaxed)};
}

// Gauge implementation
Gauge::Gauge(const std::string& name, const std::string& description)
    : Metric(name, MetricType::GAUGE, description)
{
}

Gauge::Ptr Gauge::create(const std::string& name, const std::string& description) {
    return std::shared_ptr<Gauge>(new Gauge(name, description));
}

void Gauge::increment(double value) {
    value_.fetch_add(value, std::memory_order_relaxed);
}

void Gauge::decrement(double value) {
    value_.fetch_sub(value, std::memory_order_relaxed);
}

void Gauge::set(double value) {
    value_.store(value, std::memory_order_relaxed);
}

MetricValue Gauge::get() const {
    return MetricValue{value_.load(std::memory_order_relaxed)};
}

// Histogram implementation
Histogram::Histogram(const std::string& name, const std::vector<double>& buckets, const std::string& description)
    : Metric(name, MetricType::HISTOGRAM, description)
    , bucket_boundaries_(buckets)
{
    // Initialize atomic counters
    for (size_t i = 0; i < buckets.size() + 1; i++) {
        bucket_counts_.emplace_back(0);
    }
}

Histogram::Ptr Histogram::create(const std::string& name, const std::vector<double>& buckets, const std::string& description) {
    return std::shared_ptr<Histogram>(new Histogram(name, buckets, description));
}

void Histogram::observe(double value) {
    // Find appropriate bucket
    size_t bucket_index = 0;
    for (size_t i = 0; i < bucket_boundaries_.size(); i++) {
        if (value <= bucket_boundaries_[i]) {
            bucket_index = i;
            break;
        }
        bucket_index = bucket_boundaries_.size();
    }
    
    bucket_counts_[bucket_index].fetch_add(1, std::memory_order_relaxed);
    count_.fetch_add(1, std::memory_order_relaxed);
    sum_.fetch_add(value, std::memory_order_relaxed);
}

void Histogram::reset() {
    for (size_t i = 0; i < bucket_counts_.size(); i++) {
        bucket_counts_[i].store(0, std::memory_order_relaxed);
    }
    count_.store(0, std::memory_order_relaxed);
    sum_.store(0.0, std::memory_order_relaxed);
}

MetricValue Histogram::get() const {
    return MetricValue{static_cast<int64_t>(count_.load(std::memory_order_relaxed))};
}

std::unordered_map<double, uint64_t> Histogram::buckets() const {
    std::unordered_map<double, uint64_t> result;
    for (size_t i = 0; i < bucket_boundaries_.size(); i++) {
        result[bucket_boundaries_[i]] = bucket_counts_[i].load(std::memory_order_relaxed);
    }
    result[std::numeric_limits<double>::infinity()] = bucket_counts_[bucket_boundaries_.size()].load(std::memory_order_relaxed);
    return result;
}

uint64_t Histogram::count() const {
    return count_.load(std::memory_order_relaxed);
}

double Histogram::sum() const {
    return sum_.load(std::memory_order_relaxed);
}

// MetricsRegistry implementation
MetricsRegistry::Ptr MetricsRegistry::create() {
    return std::shared_ptr<MetricsRegistry>(new MetricsRegistry());
}

MetricsRegistry::MetricsRegistry() {
}

void MetricsRegistry::register_metric(Metric::Ptr metric) {
    std::lock_guard<std::mutex> lock(mutex_);
    metrics_[metric->name()] = metric;
}

void MetricsRegistry::unregister_metric(const std::string& name) {
    std::lock_guard<std::mutex> lock(mutex_);
    metrics_.erase(name);
}

Metric::Ptr MetricsRegistry::get_metric(const std::string& name) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = metrics_.find(name);
    return it != metrics_.end() ? it->second : nullptr;
}

std::vector<Metric::Ptr> MetricsRegistry::get_all_metrics() {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<Metric::Ptr> result;
    for (const auto& [name, metric] : metrics_) {
        result.push_back(metric);
    }
    return result;
}

Counter::Ptr MetricsRegistry::counter(const std::string& name, const std::string& description) {
    auto metric = get_metric(name);
    if (!metric) {
        auto counter = Counter::create(name, description);
        register_metric(counter);
        return counter;
    }
    return std::dynamic_pointer_cast<Counter>(metric);
}

Gauge::Ptr MetricsRegistry::gauge(const std::string& name, const std::string& description) {
    auto metric = get_metric(name);
    if (!metric) {
        auto gauge = Gauge::create(name, description);
        register_metric(gauge);
        return gauge;
    }
    return std::dynamic_pointer_cast<Gauge>(metric);
}

Histogram::Ptr MetricsRegistry::histogram(const std::string& name, const std::vector<double>& buckets, const std::string& description) {
    auto metric = get_metric(name);
    if (!metric) {
        auto histogram = Histogram::create(name, buckets, description);
        register_metric(histogram);
        return histogram;
    }
    return std::dynamic_pointer_cast<Histogram>(metric);
}

std::string MetricsRegistry::export_prometheus() {
    std::lock_guard<std::mutex> lock(mutex_);
    std::ostringstream oss;
    
    for (const auto& [name, metric] : metrics_) {
        // Description
        if (!metric->description().empty()) {
            oss << "# HELP " << name << " " << metric->description() << "\n";
        }
        
        // Type
        switch (metric->type()) {
            case MetricType::COUNTER:
                oss << "# TYPE " << name << " counter\n";
                break;
            case MetricType::GAUGE:
                oss << "# TYPE " << name << " gauge\n";
                break;
            case MetricType::HISTOGRAM:
                oss << "# TYPE " << name << " histogram\n";
                break;
            default:
                break;
        }
        
        // Value
        auto value = metric->get();
        if (std::holds_alternative<double>(value)) {
            oss << name << " " << std::get<double>(value) << "\n";
        } else if (std::holds_alternative<int64_t>(value)) {
            oss << name << " " << std::get<int64_t>(value) << "\n";
        }
    }
    
    return oss.str();
}

// Span implementation
Span::Span(const std::string& name, const std::string& trace_id, const std::string& parent_span_id)
    : trace_id_(trace_id.empty() ? generate_id() : trace_id)
    , span_id_(generate_id())
    , parent_span_id_(parent_span_id)
    , name_(name)
{
}

Span::~Span() {
    if (start_time_ != std::chrono::system_clock::time_point{} && end_time_ == std::chrono::system_clock::time_point{}) {
        finish();
    }
}

std::string Span::generate_id() {
    static std::random_device rd;
    static std::mt19937 gen(rd());
    static std::uniform_int_distribution<uint64_t> dist;
    
    std::ostringstream oss;
    oss << std::hex << dist(gen);
    return oss.str();
}

void Span::set_tag(const std::string& key, const std::string& value) {
    tags_[key] = value;
}

void Span::set_status(const std::string& status) {
    status_ = status;
}

void Span::log(const std::string& message) {
    logs_.push_back(message);
}

void Span::start() {
    start_time_ = std::chrono::system_clock::now();
}

void Span::finish() {
    end_time_ = std::chrono::system_clock::now();
}

std::chrono::microseconds Span::duration() const {
    if (start_time_ == std::chrono::system_clock::time_point{} || 
        end_time_ == std::chrono::system_clock::time_point{}) {
        return std::chrono::microseconds{0};
    }
    return std::chrono::duration_cast<std::chrono::microseconds>(end_time_ - start_time_);
}

// Tracer implementation
Tracer::Tracer(const std::string& service_name)
    : service_name_(service_name)
{
}

Tracer::Ptr Tracer::create(const std::string& service_name) {
    return std::shared_ptr<Tracer>(new Tracer(service_name));
}

Span::Ptr Tracer::start_span(const std::string& name, Span::Ptr parent) {
    std::string trace_id = current_trace_id_;
    std::string parent_span_id;
    
    if (parent) {
        trace_id = parent->trace_id();
        parent_span_id = parent->span_id();
    }
    
    auto span = std::make_shared<Span>(name, trace_id, parent_span_id);
    span->start();
    
    if (trace_id.empty()) {
        current_trace_id_ = span->trace_id();
    }
    
    return span;
}

// PerformanceMonitor implementation
PerformanceMonitor::PerformanceMonitor() {
}

PerformanceMonitor::Ptr PerformanceMonitor::create() {
    return std::shared_ptr<PerformanceMonitor>(new PerformanceMonitor());
}

void PerformanceMonitor::record_operation(const std::string& operation, std::chrono::microseconds duration) {
    std::lock_guard<std::mutex> lock(mutex_);
    operations_[operation].push_back(duration);
}

std::unordered_map<std::string, std::vector<std::chrono::microseconds>> PerformanceMonitor::get_operations() {
    std::lock_guard<std::mutex> lock(mutex_);
    return operations_;
}

std::chrono::microseconds PerformanceMonitor::get_average_duration(const std::string& operation) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto it = operations_.find(operation);
    if (it == operations_.end() || it->second.empty()) {
        return std::chrono::microseconds{0};
    }
    
    auto sum = std::chrono::microseconds{0};
    for (const auto& duration : it->second) {
        sum += duration;
    }
    
    return sum / static_cast<int64_t>(it->second.size());
}

std::chrono::microseconds PerformanceMonitor::get_p95_duration(const std::string& operation) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto it = operations_.find(operation);
    if (it == operations_.end() || it->second.empty()) {
        return std::chrono::microseconds{0};
    }
    
    auto durations = it->second;
    std::sort(durations.begin(), durations.end());
    
    size_t index = static_cast<size_t>(durations.size() * 0.95);
    if (index >= durations.size()) {
        index = durations.size() - 1;
    }
    
    return durations[index];
}

std::chrono::microseconds PerformanceMonitor::get_p99_duration(const std::string& operation) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto it = operations_.find(operation);
    if (it == operations_.end() || it->second.empty()) {
        return std::chrono::microseconds{0};
    }
    
    auto durations = it->second;
    std::sort(durations.begin(), durations.end());
    
    size_t index = static_cast<size_t>(durations.size() * 0.99);
    if (index >= durations.size()) {
        index = durations.size() - 1;
    }
    
    return durations[index];
}

void PerformanceMonitor::reset() {
    std::lock_guard<std::mutex> lock(mutex_);
    operations_.clear();
}

// Alert implementation
Alert::Alert(const std::string& name, const std::string& description)
    : name_(name)
    , description_(description)
{
}

void Alert::trigger() {
    if (!active_) {
        active_ = true;
        if (callback_) {
            callback_(*this);
        }
    }
}

void Alert::resolve() {
    if (active_) {
        active_ = false;
    }
}

void Alert::set_callback(Callback callback) {
    callback_ = callback;
}

// AlertManager implementation
AlertManager::AlertManager() {
}

AlertManager::Ptr AlertManager::create() {
    return std::shared_ptr<AlertManager>(new AlertManager());
}

void AlertManager::register_alert(Alert::Ptr alert) {
    std::lock_guard<std::mutex> lock(mutex_);
    alerts_[alert->name()] = alert;
}

void AlertManager::unregister_alert(const std::string& name) {
    std::lock_guard<std::mutex> lock(mutex_);
    alerts_.erase(name);
}

Alert::Ptr AlertManager::get_alert(const std::string& name) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = alerts_.find(name);
    return it != alerts_.end() ? it->second : nullptr;
}

std::vector<Alert::Ptr> AlertManager::get_active_alerts() {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<Alert::Ptr> result;
    for (const auto& [name, alert] : alerts_) {
        if (alert->is_active()) {
            result.push_back(alert);
        }
    }
    return result;
}

// Global monitoring instance
std::shared_ptr<MetricsRegistry> Monitoring::metrics_ = nullptr;
std::shared_ptr<Tracer> Monitoring::tracer_ = nullptr;
std::shared_ptr<PerformanceMonitor> Monitoring::performance_ = nullptr;
std::shared_ptr<AlertManager> Monitoring::alerts_ = nullptr;

MetricsRegistry::Ptr Monitoring::metrics() {
    if (!metrics_) {
        metrics_ = MetricsRegistry::create();
    }
    return metrics_;
}

Tracer::Ptr Monitoring::tracer() {
    return tracer_;
}

PerformanceMonitor::Ptr Monitoring::performance() {
    if (!performance_) {
        performance_ = PerformanceMonitor::create();
    }
    return performance_;
}

AlertManager::Ptr Monitoring::alerts() {
    if (!alerts_) {
        alerts_ = AlertManager::create();
    }
    return alerts_;
}

void Monitoring::initialize(const std::string& service_name) {
    metrics_ = MetricsRegistry::create();
    tracer_ = Tracer::create(service_name);
    performance_ = PerformanceMonitor::create();
    alerts_ = AlertManager::create();
}

} // namespace monitoring
} // namespace best_server