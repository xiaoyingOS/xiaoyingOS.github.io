#pragma once

#include <best_server/future/future.hpp>
#include <string>
#include <vector>
#include <memory>
#include <functional>
#include <chrono>

namespace best_server {
namespace service_discovery {

// Service instance
struct ServiceInstance {
    std::string id;
    std::string name;
    std::string address;
    uint16_t port;
    std::unordered_map<std::string, std::string> metadata;
    std::chrono::steady_clock::time_point last_seen;
    bool healthy{true};
    
    std::string endpoint() const;
};

// Service discovery configuration
struct Config {
    std::string discovery_server;  // e.g., consul:8500, etcd:2379
    std::string service_name;
    std::string service_address;
    uint16_t service_port;
    std::string health_check_url;
    uint32_t health_check_interval{10};  // seconds
    uint32_t deregister_timeout{30};     // seconds
    std::unordered_map<std::string, std::string> tags;
};

// Service discovery interface
class ServiceDiscovery {
public:
    using Ptr = std::shared_ptr<ServiceDiscovery>;
    
    // Create service discovery (auto-detect backend)
    static future::Future<Ptr> create(const Config& config);
    
    virtual ~ServiceDiscovery() = default;
    
    // Register service
    virtual future::Future<bool> register_service(const ServiceInstance& instance) = 0;
    
    // Deregister service
    virtual future::Future<bool> deregister_service(const std::string& service_id) = 0;
    
    // Discover services
    virtual future::Future<std::vector<ServiceInstance>> discover(const std::string& service_name) = 0;
    
    // Watch for service changes
    virtual void watch(const std::string& service_name,
                      std::function<void(const std::vector<ServiceInstance>&)> callback) = 0;
    
    // Health check
    virtual future::Future<bool> health_check(const std::string& service_id) = 0;
    
    // Get configuration
    const Config& config() const { return config_; }
    
protected:
    ServiceDiscovery(const Config& config);
    
    Config config_;
};

// Consul-based service discovery
class ConsulServiceDiscovery : public ServiceDiscovery {
public:
    static future::Future<Ptr> create(const Config& config);
    
    future::Future<bool> register_service(const ServiceInstance& instance) override;
    future::Future<bool> deregister_service(const std::string& service_id) override;
    future::Future<std::vector<ServiceInstance>> discover(const std::string& service_name) override;
    void watch(const std::string& service_name,
              std::function<void(const std::vector<ServiceInstance>&)> callback) override;
    future::Future<bool> health_check(const std::string& service_id) override;
    
private:
    ConsulServiceDiscovery(const Config& config);
    
    std::string consul_url_;
};

// etcd-based service discovery
class EtcdServiceDiscovery : public ServiceDiscovery {
public:
    static future::Future<Ptr> create(const Config& config);
    
    future::Future<bool> register_service(const ServiceInstance& instance) override;
    future::Future<bool> deregister_service(const std::string& service_id) override;
    future::Future<std::vector<ServiceInstance>> discover(const std::string& service_name) override;
    void watch(const std::string& service_name,
              std::function<void(const std::vector<ServiceInstance>&)> callback) override;
    future::Future<bool> health_check(const std::string& service_id) override;
    
private:
    EtcdServiceDiscovery(const Config& config);
    
    std::string etcd_endpoints_;
};

// DNS-based service discovery (SRV records)
class DNSServiceDiscovery : public ServiceDiscovery {
public:
    static future::Future<Ptr> create(const Config& config);
    
    future::Future<bool> register_service(const ServiceInstance& instance) override;
    future::Future<bool> deregister_service(const std::string& service_id) override;
    future::Future<std::vector<ServiceInstance>> discover(const std::string& service_name) override;
    void watch(const std::string& service_name,
              std::function<void(const std::vector<ServiceInstance>&)> callback) override;
    future::Future<bool> health_check(const std::string& service_id) override;
    
private:
    DNSServiceDiscovery(const Config& config);
    
    std::string dns_server_;
};

// Service registry (simple in-memory implementation)
class ServiceRegistry {
public:
    using Ptr = std::shared_ptr<ServiceRegistry>;
    
    static Ptr create();
    
    void register_service(const ServiceInstance& instance);
    void deregister_service(const std::string& service_id);
    std::vector<ServiceInstance> discover(const std::string& service_name);
    ServiceInstance* get_service(const std::string& service_id);
    
    void cleanup_expired(std::chrono::seconds ttl);
    
private:
    ServiceRegistry();
    
    std::unordered_map<std::string, ServiceInstance> services_;
    mutable std::mutex mutex_;
};

// Utilities
namespace utils {
    // Generate service ID
    std::string generate_service_id(const std::string& name, const std::string& address, uint16_t port);
    
    // Parse service endpoint
    bool parse_endpoint(const std::string& endpoint, std::string& address, uint16_t& port);
    
    // Build service endpoint
    std::string build_endpoint(const std::string& address, uint16_t port);
}

} // namespace service_discovery
} // namespace best_server