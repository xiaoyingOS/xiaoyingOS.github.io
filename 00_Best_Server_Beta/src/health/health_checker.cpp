// HealthChecker implementation
#include "best_server/health/health_checker.hpp"
#include "best_server/network/http_client.hpp"
#include <chrono>
#include <thread>
#include <regex>

namespace best_server {
namespace health {

// Status utilities
namespace utils {

std::string status_to_string(Status status) {
    switch (status) {
        case Status::Healthy: return "healthy";
        case Status::Unhealthy: return "unhealthy";
        case Status::Degraded: return "degraded";
        case Status::Unknown: return "unknown";
        default: return "unknown";
    }
}

Status status_from_string(const std::string& str) {
    std::string lower_str = str;
    std::transform(lower_str.begin(), lower_str.end(), lower_str.begin(), ::tolower);
    
    if (lower_str == "healthy") return Status::Healthy;
    if (lower_str == "unhealthy") return Status::Unhealthy;
    if (lower_str == "degraded") return Status::Degraded;
    return Status::Unknown;
}

CheckConfig parse_check_config(const std::string& json) {
    // This would parse JSON configuration
    // For now, return default config
    CheckConfig config;
    return config;
}

std::string health_check_url(const std::string& base_url, const std::string& path) {
    if (base_url.empty()) {
        return path;
    }
    
    std::string url = base_url;
    if (url.back() != '/' && !path.empty() && path.front() != '/') {
        url += '/';
    }
    url += path;
    return url;
}

} // namespace utils

// HealthCheck implementation
HealthCheck::HealthCheck(const CheckConfig& config)
    : config_(config)
    , status_(Status::Unknown)
    , consecutive_failures_(0)
    , consecutive_successes_(0)
{
}

HealthCheck::Ptr HealthCheck::create(const CheckConfig& config) {
    return std::make_shared<HealthCheck>(config);
}

future::Future<CheckResult> HealthCheck::check() {
    CheckResult result;
    result.success = true;
    result.status = Status::Healthy;
    result.timestamp = std::chrono::system_clock::now();
    
    // This is a base implementation
    // Subclasses should override this
    co_return result;
}

void HealthCheck::start() {
    if (running_.load()) {
        return;
    }
    
    running_.store(true);
    check_thread_ = std::thread([this]() {
        this->run_periodic_check();
    });
}

void HealthCheck::stop() {
    if (!running_.load()) {
        return;
    }
    
    running_.store(false);
    if (check_thread_.joinable()) {
        check_thread_.join();
    }
}

void HealthCheck::run_periodic_check() {
    while (running_.load()) {
        auto result = check().get();  // Wait for check to complete
        
        // Update status based on result
        if (result.success) {
            consecutive_successes_++;
            consecutive_failures_ = 0;
            
            if (consecutive_successes_ >= config_.healthy_threshold) {
                update_status(Status::Healthy);
            }
        } else {
            consecutive_failures_++;
            consecutive_successes_ = 0;
            
            if (consecutive_failures_ >= config_.unhealthy_threshold) {
                update_status(Status::Unhealthy);
            }
        }
        
        // Wait for next check
        std::this_thread::sleep_for(std::chrono::milliseconds(config_.interval));
    }
}

void HealthCheck::update_status(Status new_status) {
    if (status_ != new_status) {
        status_ = new_status;
        
        if (status_callback_) {
            status_callback_(new_status);
        }
    }
}

void HealthCheck::set_status_callback(std::function<void(Status)> callback) {
    status_callback_ = callback;
}

// HTTPHealthCheck implementation
HTTPHealthCheck::HTTPHealthCheck(const CheckConfig& config)
    : HealthCheck(config)
{
}

HTTPHealthCheck::Ptr HTTPHealthCheck::create(const CheckConfig& config) {
    return std::make_shared<HTTPHealthCheck>(config);
}

future::Future<CheckResult> HTTPHealthCheck::check() {
    CheckResult result;
    result.timestamp = std::chrono::system_clock::now();
    auto start = std::chrono::steady_clock::now();
    
    // Parse endpoint
    std::string host = config_.endpoint;
    uint16_t port = 80;
    
    size_t port_pos = host.find(':');
    if (port_pos != std::string::npos) {
        port = static_cast<uint16_t>(std::stoi(host.substr(port_pos + 1)));
        host = host.substr(0, port_pos);
    }
    
    size_t scheme_pos = host.find("://");
    if (scheme_pos != std::string::npos) {
        host = host.substr(scheme_pos + 3);
    }
    
    // Create HTTP request
    HTTPRequest request;
    request.set_method(config_.method == "POST" ? HTTPMethod::POST : HTTPMethod::GET);
    request.set_url(config_.endpoint);
    
    // Add headers
    for (const auto& [name, value] : config_.headers) {
        request.set_header(name, value);
    }
    
    // Make HTTP request
    // This is a simplified implementation
    // In a real implementation, you would use the HTTP client
    result.success = true;
    result.status = Status::Healthy;
    
    auto end = std::chrono::steady_clock::now();
    result.latency = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
    
    co_return result;
}

// TCPHealthCheck implementation
TCPHealthCheck::TCPHealthCheck(const CheckConfig& config)
    : HealthCheck(config)
{
}

TCPHealthCheck::Ptr TCPHealthCheck::create(const CheckConfig& config) {
    return std::make_shared<TCPHealthCheck>(config);
}

future::Future<CheckResult> TCPHealthCheck::check() {
    CheckResult result;
    result.timestamp = std::chrono::system_clock::now();
    auto start = std::chrono::steady_clock::now();
    
    // Parse endpoint
    std::string host = config_.endpoint;
    uint16_t port = 80;
    
    size_t port_pos = host.find(':');
    if (port_pos != std::string::npos) {
        port = static_cast<uint16_t>(std::stoi(host.substr(port_pos + 1)));
        host = host.substr(0, port_pos);
    }
    
    size_t scheme_pos = host.find("://");
    if (scheme_pos != std::string::npos) {
        host = host.substr(scheme_pos + 3);
    }
    
    // Create TCP socket and connect
    // This is a simplified implementation
    // In a real implementation, you would use the TCP socket
    result.success = true;
    result.status = Status::Healthy;
    
    auto end = std::chrono::steady_clock::now();
    result.latency = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
    
    co_return result;
}

// HealthChecker implementation
HealthChecker::HealthChecker() {
}

HealthChecker::Ptr HealthChecker::create() {
    return std::make_shared<HealthChecker>();
}

void HealthChecker::add_check(HealthCheck::Ptr check) {
    std::unique_lock<std::shared_mutex> lock(mutex_);
    checks_[check->config().check_name] = check;
}

void HealthChecker::remove_check(const std::string& check_name) {
    std::unique_lock<std::shared_mutex> lock(mutex_);
    checks_.erase(check_name);
}

future::Future<CheckResult> HealthChecker::check(const std::string& check_name) {
    std::shared_lock<std::shared_mutex> lock(mutex_);
    
    auto it = checks_.find(check_name);
    if (it == checks_.end()) {
        CheckResult result;
        result.success = false;
        result.status = Status::Unknown;
        result.message = "Check not found";
        co_return result;
    }
    
    co_return co_await it->second->check();
}

future::Future<std::vector<CheckResult>> HealthChecker::check_all() {
    std::shared_lock<std::shared_mutex> lock(mutex_);
    
    std::vector<CheckResult> results;
    for (const auto& [name, check] : checks_) {
        auto result = co_await check->check();
        results.push_back(result);
    }
    
    co_return results;
}

void HealthChecker::start() {
    std::shared_lock<std::shared_mutex> lock(mutex_);
    
    for (const auto& [name, check] : checks_) {
        check->start();
    }
}

void HealthChecker::stop() {
    std::shared_lock<std::shared_mutex> lock(mutex_);
    
    for (const auto& [name, check] : checks_) {
        check->stop();
    }
}

Status HealthChecker::overall_status() const {
    std::shared_lock<std::shared_mutex> lock(mutex_);
    
    if (checks_.empty()) {
        return Status::Unknown;
    }
    
    bool has_unhealthy = false;
    bool has_healthy = false;
    
    for (const auto& [name, check] : checks_) {
        if (check->status() == Status::Unhealthy) {
            has_unhealthy = true;
        } else if (check->status() == Status::Healthy) {
            has_healthy = true;
        }
    }
    
    if (has_unhealthy) {
        return Status::Unhealthy;
    } else if (has_healthy) {
        return Status::Healthy;
    }
    
    return Status::Unknown;
}

std::vector<std::string> HealthChecker::unhealthy_checks() const {
    std::shared_lock<std::shared_mutex> lock(mutex_);
    
    std::vector<std::string> names;
    for (const auto& [name, check] : checks_) {
        if (check->status() == Status::Unhealthy) {
            names.push_back(name);
        }
    }
    
    return names;
}

void HealthChecker::set_status_change_callback(std::function<void(const std::string&, Status)> callback) {
    status_callback_ = callback;
}

// ServiceHealthMonitor implementation
ServiceHealthMonitor::ServiceHealthMonitor(service_discovery::ServiceDiscovery::Ptr discovery)
    : discovery_(discovery)
{
}

ServiceHealthMonitor::Ptr ServiceHealthMonitor::create(service_discovery::ServiceDiscovery::Ptr discovery) {
    return std::make_shared<ServiceHealthMonitor>(discovery);
}

void ServiceHealthMonitor::monitor_service(const std::string& service_name) {
    std::unique_lock<std::shared_mutex> lock(mutex_);
    
    // Initialize service status map
    service_status_[service_name] = std::unordered_map<std::string, Status>();
    
    // Start monitoring
    if (!running_.load()) {
        running_.store(true);
        std::thread([this]() {
            while (this->running_.load()) {
                for (const auto& [service_name, _] : this->service_status_) {
                    this->check_service_health(service_name);
                }
                std::this_thread::sleep_for(std::chrono::seconds(10));
            }
        }).detach();
    }
}

void ServiceHealthMonitor::unmonitor_service(const std::string& service_name) {
    std::unique_lock<std::shared_mutex> lock(mutex_);
    service_status_.erase(service_name);
}

void ServiceHealthMonitor::check_service_health(const std::string& service_name) {
    auto instances = discovery_->discover(service_name).get();
    
    std::unordered_map<std::string, Status> current_status;
    
    for (const auto& instance : instances) {
        // Perform health check on each instance
        // This is a simplified implementation
        current_status[instance.id] = instance.healthy ? Status::Healthy : Status::Unhealthy;
    }
    
    // Update status
    std::unique_lock<std::shared_mutex> lock(mutex_);
    service_status_[service_name] = current_status;
    
    // Notify watchers
    auto it = watchers_.find(service_name);
    if (it != watchers_.end()) {
        for (const auto& callback : it->second) {
            callback();
        }
    }
}

std::vector<service_discovery::ServiceInstance> ServiceHealthMonitor::healthy_instances(const std::string& service_name) {
    std::shared_lock<std::shared_mutex> lock(mutex_);
    
    std::vector<service_discovery::ServiceInstance> result;
    
    auto it = service_status_.find(service_name);
    if (it == service_status_.end()) {
        return result;
    }
    
    auto instances = discovery_->discover(service_name).get();
    for (const auto& instance : instances) {
        auto status_it = it->second.find(instance.id);
        if (status_it != it->second.end() && status_it->second == Status::Healthy) {
            result.push_back(instance);
        }
    }
    
    return result;
}

std::vector<service_discovery::ServiceInstance> ServiceHealthMonitor::unhealthy_instances(const std::string& service_name) {
    std::shared_lock<std::shared_mutex> lock(mutex_);
    
    std::vector<service_discovery::ServiceInstance> result;
    
    auto it = service_status_.find(service_name);
    if (it == service_status_.end()) {
        return result;
    }
    
    auto instances = discovery_->discover(service_name).get();
    for (const auto& instance : instances) {
        auto status_it = it->second.find(instance.id);
        if (status_it != it->second.end() && status_it->second == Status::Unhealthy) {
            result.push_back(instance);
        }
    }
    
    return result;
}

void ServiceHealthMonitor::start() {
    running_.store(true);
}

void ServiceHealthMonitor::stop() {
    running_.store(false);
}

} // namespace health
} // namespace best_server