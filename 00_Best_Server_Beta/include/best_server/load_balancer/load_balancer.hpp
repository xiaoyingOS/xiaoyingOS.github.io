#pragma once

#include <best_server/service_discovery/service_discovery.hpp>
#include <best_server/health/health_checker.hpp>
#include <string>
#include <vector>
#include <memory>
#include <functional>
#include <random>
#include <map>
#include <unordered_map>
#include <mutex>
#include <atomic>
#include <chrono>

namespace best_server {
namespace load_balancer {

// Load balancing strategies
enum class Strategy {
    RoundRobin,
    Random,
    LeastConnections,
    WeightedRoundRobin,
    IPHash,
    ConsistentHash,
    LeastResponseTime
};

// Backend server
struct Backend {
    std::string id;
    std::string address;
    uint16_t port;
    uint32_t weight{100};  // For weighted strategies
    uint32_t active_connections{0};
    uint64_t total_requests{0};
    std::chrono::microseconds avg_response_time{0};
    bool healthy{true};
    
    std::string endpoint() const;
};

// Load balancer
class LoadBalancer {
public:
    using Ptr = std::shared_ptr<LoadBalancer>;
    
    static Ptr create(Strategy strategy = Strategy::RoundRobin);
    
    // Add backend
    void add_backend(const Backend& backend);
    void remove_backend(const std::string& id);
    
    // Get next backend
    Backend* next_backend(const std::string& client_ip = "");
    
    // Update backend metrics
    void update_backend(const std::string& id, const std::chrono::microseconds& response_time);
    
    // Mark backend as healthy/unhealthy
    void set_backend_health(const std::string& id, bool healthy);
    
    // Get all backends
    std::vector<Backend> backends() const;
    std::vector<Backend> healthy_backends() const;
    
    // Set strategy
    void set_strategy(Strategy strategy);
    Strategy strategy() const { return strategy_; }
    
    // Get statistics
    struct Statistics {
        uint64_t total_requests{0};
        uint64_t total_errors{0};
        std::chrono::microseconds avg_response_time{0};
        size_t active_connections{0};
        size_t healthy_backends{0};
    };
    Statistics statistics() const;
    
private:
    LoadBalancer(Strategy strategy);
    
    Backend* round_robin();
    Backend* random();
    Backend* least_connections();
    Backend* weighted_round_robin();
    Backend* ip_hash(const std::string& client_ip);
    Backend* consistent_hash(const std::string& key);
    Backend* least_response_time();
    
    Strategy strategy_;
    std::unordered_map<std::string, Backend> backends_;
    std::vector<std::string> backend_order_;  // For round-robin
    size_t current_index_{0};
    mutable std::mutex mutex_;
    Statistics stats_;
    std::mt19937 rng_;
};

// Service-aware load balancer (integrates with service discovery)
class ServiceLoadBalancer {
public:
    using Ptr = std::shared_ptr<ServiceLoadBalancer>;
    
    static Ptr create(service_discovery::ServiceDiscovery::Ptr discovery,
                     health::HealthChecker::Ptr health_checker,
                     Strategy strategy = Strategy::LeastConnections);
    
    // Get next backend for service
    future::Future<Backend*> get_backend(const std::string& service_name,
                                        const std::string& client_ip = "");
    
    // Update backend after request
    void update_backend(const std::string& service_name, const std::string& backend_id,
                       const std::chrono::microseconds& response_time, bool success);
    
    // Set strategy per service
    void set_strategy(const std::string& service_name, Strategy strategy);
    
    // Get statistics for service
    LoadBalancer::Statistics service_statistics(const std::string& service_name) const;
    
    // Refresh backends from service discovery
    future::Future<void> refresh_backends(const std::string& service_name);
    
private:
    ServiceLoadBalancer(service_discovery::ServiceDiscovery::Ptr discovery,
                       health::HealthChecker::Ptr health_checker,
                       Strategy strategy);
    
    service_discovery::ServiceDiscovery::Ptr discovery_;
    health::HealthChecker::Ptr health_checker_;
    Strategy default_strategy_;
    std::unordered_map<std::string, LoadBalancer::Ptr> service_balancers_;
    std::unordered_map<std::string, Strategy> service_strategies_;
    mutable std::mutex mutex_;
};

// Consistent hash ring
class ConsistentHashRing {
public:
    ConsistentHashRing(uint32_t virtual_nodes = 150);
    
    void add_node(const std::string& node_id, const Backend& backend);
    void remove_node(const std::string& node_id);
    
    Backend* get_node(const std::string& key);
    
    std::vector<std::string> get_nodes() const;
    
private:
    uint32_t hash(const std::string& key) const;
    
    uint32_t virtual_nodes_;
    std::map<uint32_t, std::pair<std::string, Backend>> ring_;
    std::unordered_map<std::string, std::vector<uint32_t>> node_hashes_;
};

// Load balancing utilities
namespace utils {
    // Parse strategy from string
    Strategy parse_strategy(const std::string& str);
    
    // Convert strategy to string
    std::string strategy_to_string(Strategy strategy);
    
    // Calculate hash for IP
    uint32_t hash_ip(const std::string& ip);
    
    // Calculate hash for consistent hashing
    uint32_t hash_consistent(const std::string& key);
    
    // Normalize weight
    uint32_t normalize_weight(uint32_t weight);
    
    // Calculate weighted index
    size_t weighted_index(const std::vector<uint32_t>& weights, uint32_t total_weight);
}

} // namespace load_balancer
} // namespace best_server