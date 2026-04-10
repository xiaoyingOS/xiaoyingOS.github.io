#pragma once

#include <best_server/future/future.hpp>
#include <best_server/service_discovery/service_discovery.hpp>
#include <string>
#include <vector>
#include <memory>
#include <functional>
#include <chrono>
#include <thread>
#include <mutex>
#include <atomic>

namespace best_server {
namespace health {

// Health status
enum class Status {
    Healthy,
    Unhealthy,
    Degraded,
    Unknown
};

// Health check result
struct CheckResult {
    bool success;
    Status status;
    std::string message;
    std::chrono::milliseconds latency;
    std::chrono::system_clock::time_point timestamp;
    
    CheckResult() : success(false), status(Status::Unknown), latency(0) {}
};

// Health check configuration
struct CheckConfig {
    std::string check_name;
    std::string endpoint;           // URL or address to check
    std::string method{"GET"};      // HTTP method
    std::string expected_response;  // Expected response content
    uint16_t expected_status{200};  // Expected HTTP status code
    uint32_t timeout{5000};         // Check timeout in ms
    uint32_t interval{10000};       // Check interval in ms
    uint32_t unhealthy_threshold{3};  // Consecutive failures before marking unhealthy
    uint32_t healthy_threshold{2};    // Consecutive successes before marking healthy
    bool enable_tls{false};
    std::unordered_map<std::string, std::string> headers;
};

// Health check
class HealthCheck {
public:
    using Ptr = std::shared_ptr<HealthCheck>;

    static Ptr create(const CheckConfig& config);

    virtual future::Future<CheckResult> check();

    void start();
    void stop();

    Status status() const { return status_; }
    const CheckConfig& config() const { return config_; }

    // Set callback for status changes
    void set_status_callback(std::function<void(Status)> callback);

    virtual ~HealthCheck() = default;

private:
    HealthCheck(const CheckConfig& config);

    void run_periodic_check();
    void update_status(Status new_status);

    CheckConfig config_;
    Status status_{Status::Unknown};
    std::atomic<bool> running_{false};
    uint32_t consecutive_failures_{0};
    uint32_t consecutive_successes_{0};
    std::function<void(Status)> status_callback_;
    std::thread check_thread_;
};

// Health checker (manages multiple checks)
class HealthChecker {
public:
    using Ptr = std::shared_ptr<HealthChecker>;

    static Ptr create();

    void add_check(HealthCheck::Ptr check);
    void remove_check(const std::string& check_name);

    future::Future<CheckResult> check(const std::string& check_name);
    future::Future<std::vector<CheckResult>> check_all();

    void start();
    void stop();

    Status overall_status() const;
    std::vector<std::string> unhealthy_checks() const;

    void set_status_change_callback(std::function<void(const std::string&, Status)> callback);

private:
    HealthChecker();

    std::unordered_map<std::string, HealthCheck::Ptr> checks_;
    std::mutex mutex_;
    std::function<void(const std::string&, Status)> status_callback_;
};

// HTTP health check
class HTTPHealthCheck : public HealthCheck {
public:
    static Ptr create(const CheckConfig& config);
    
    future::Future<CheckResult> check() override;
    
private:
    HTTPHealthCheck(const CheckConfig& config);
};

// TCP health check
class TCPHealthCheck : public HealthCheck {
public:
    static Ptr create(const CheckConfig& config);
    
    future::Future<CheckResult> check() override;
    
private:
    TCPHealthCheck(const CheckConfig& config);
};

// Service health monitor (integrates with service discovery)
class ServiceHealthMonitor {
public:
    using Ptr = std::shared_ptr<ServiceHealthMonitor>;
    
    static Ptr create(service_discovery::ServiceDiscovery::Ptr discovery);
    
    void monitor_service(const std::string& service_name);
    void unmonitor_service(const std::string& service_name);
    
    std::vector<service_discovery::ServiceInstance> healthy_instances(const std::string& service_name);
    std::vector<service_discovery::ServiceInstance> unhealthy_instances(const std::string& service_name);
    
    void start();
    void stop();
    
private:
    ServiceHealthMonitor(service_discovery::ServiceDiscovery::Ptr discovery);

    void check_service_health(const std::string& service_name);

    service_discovery::ServiceDiscovery::Ptr discovery_;
    std::unordered_map<std::string, std::unordered_map<std::string, Status>> service_status_;
    std::unordered_map<std::string, std::vector<std::function<void()>>> watchers_;
    std::mutex mutex_;
    std::atomic<bool> running_{false};
};

// Utilities
namespace utils {
    // Parse health check configuration
    CheckConfig parse_check_config(const std::string& json);
    
    // Convert status to string
    std::string status_to_string(Status status);
    
    // Parse status from string
    Status status_from_string(const std::string& str);
    
    // Generate health check endpoint
    std::string health_check_url(const std::string& base_url, const std::string& path = "/health");
}

} // namespace health
} // namespace best_server