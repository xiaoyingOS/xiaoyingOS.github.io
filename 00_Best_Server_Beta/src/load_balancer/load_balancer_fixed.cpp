// LoadBalancer implementation
#include "best_server/load_balancer/load_balancer.hpp"
#include <coroutine>
#include <algorithm>
#include <numeric>
#include <random>

namespace best_server {
namespace load_balancer {

// Backend implementation
std::string Backend::endpoint() const {
    return address + ":" + std::to_string(port);
}

// Utilities implementation
namespace utils {

Strategy parse_strategy(const std::string& str) {
    std::string lower_str = str;
    std::transform(lower_str.begin(), lower_str.end(), lower_str.begin(), ::tolower);
    
    if (lower_str == "round_robin") return Strategy::RoundRobin;
    if (lower_str == "random") return Strategy::Random;
    if (lower_str == "least_connections") return Strategy::LeastConnections;
    if (lower_str == "weighted_round_robin") return Strategy::WeightedRoundRobin;
    if (lower_str == "ip_hash") return Strategy::IPHash;
    if (lower_str == "consistent_hash") return Strategy::ConsistentHash;
    if (lower_str == "least_response_time") return Strategy::LeastResponseTime;
    
    return Strategy::RoundRobin;
}

std::string strategy_to_string(Strategy strategy) {
    switch (strategy) {
        case Strategy::RoundRobin: return "round_robin";
        case Strategy::Random: return "random";
        case Strategy::LeastConnections: return "least_connections";
        case Strategy::WeightedRoundRobin: return "weighted_round_robin";
        case Strategy::IPHash: return "ip_hash";
        case Strategy::ConsistentHash: return "consistent_hash";
        case Strategy::LeastResponseTime: return "least_response_time";
        default: return "round_robin";
    }
}

uint32_t hash_ip(const std::string& ip) {
    uint32_t hash = 0;
    for (char c : ip) {
        hash = hash * 31 + c;
    }
    return hash;
}

uint32_t hash_consistent(const std::string& key) {
    uint32_t hash = 0;
    for (char c : key) {
        hash = hash * 31 + c;
    }
    return hash;
}

uint32_t normalize_weight(uint32_t weight) {
    if (weight == 0) {
        return 1;
    }
    if (weight > 1000) {
        return 1000;
    }
    return weight;
}

size_t weighted_index(const std::vector<uint32_t>& weights, uint32_t total_weight) {
    if (weights.empty()) {
        return 0;
    }
    
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<uint32_t> dist(1, total_weight);
    
    uint32_t random = dist(gen);
    uint32_t sum = 0;
    
    for (size_t i = 0; i < weights.size(); i++) {
        sum += weights[i];
        if (random <= sum) {
            return i;
        }
    }
    
    return weights.size() - 1;
}

} // namespace utils

// ConsistentHashRing implementation
ConsistentHashRing::ConsistentHashRing(uint32_t virtual_nodes)
    : virtual_nodes_(virtual_nodes)
{
}

void ConsistentHashRing::add_node(const std::string& node_id, const Backend& backend) {
    for (uint32_t i = 0; i < virtual_nodes_; i++) {
        std::string key = node_id + ":" + std::to_string(i);
        uint32_t hash = utils::hash_consistent(key);
        ring_[hash] = {node_id, backend};
        node_hashes_[node_id].push_back(hash);
    }
}

void ConsistentHashRing::remove_node(const std::string& node_id) {
    auto it = node_hashes_.find(node_id);
    if (it == node_hashes_.end()) {
        return;
    }
    
    for (uint32_t hash : it->second) {
        ring_.erase(hash);
    }
    
    node_hashes_.erase(it);
}

Backend* ConsistentHashRing::get_node(const std::string& key) {
    if (ring_.empty()) {
        return nullptr;
    }
    
    uint32_t hash = utils::hash_consistent(key);
    auto it = ring_.lower_bound(hash);
    
    if (it == ring_.end()) {
        it = ring_.begin();
    }
    
    return &it->second.second;
}

std::vector<std::string> ConsistentHashRing::get_nodes() const {
    std::vector<std::string> nodes;
    for (const auto& [node_id, _] : node_hashes_) {
        nodes.push_back(node_id);
    }
    return nodes;
}

uint32_t ConsistentHashRing::hash(const std::string& key) const {
    return utils::hash_consistent(key);
}

// LoadBalancer implementation
LoadBalancer::LoadBalancer(Strategy strategy)
    : strategy_(strategy)
    , current_index_(0)
    , rng_(std::random_device{}())
{
}

LoadBalancer::Ptr LoadBalancer::create(Strategy strategy) {
    return std::shared_ptr<LoadBalancer>(new LoadBalancer(strategy));
}

void LoadBalancer::add_backend(const Backend& backend) {
    std::lock_guard<std::mutex> lock(mutex_);
    backends_[backend.id] = backend;
    backend_order_.push_back(backend.id);
}

void LoadBalancer::remove_backend(const std::string& id) {
    std::lock_guard<std::mutex> lock(mutex_);
    backends_.erase(id);
    backend_order_.erase(
        std::remove(backend_order_.begin(), backend_order_.end(), id),
        backend_order_.end()
    );
    if (current_index_ >= backend_order_.size()) {
        current_index_ = 0;
    }
}

Backend* LoadBalancer::next_backend(const std::string& client_ip) {
    // Don't hold lock while calling strategy functions to avoid deadlock
    // Get the strategy first
    Strategy strategy;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        strategy = strategy_;
    }
    
    // Call the appropriate strategy function (which will acquire its own lock)
    switch (strategy) {
        case Strategy::RoundRobin:
            return round_robin();
        case Strategy::Random:
            return random();
        case Strategy::LeastConnections:
            return least_connections();
        case Strategy::WeightedRoundRobin:
            return weighted_round_robin();
        case Strategy::IPHash:
            return ip_hash(client_ip);
        case Strategy::ConsistentHash:
            return consistent_hash(client_ip);
        case Strategy::LeastResponseTime:
            return least_response_time();
        default:
            return round_robin();
    }
}

Backend* LoadBalancer::round_robin() {
    std::lock_guard<std::mutex> lock(mutex_);
    
    std::vector<Backend*> healthy_backends;
    for (auto& [id, backend] : backends_) {
        if (backend.healthy) {
            healthy_backends.push_back(&backend);
        }
    }
    
    if (healthy_backends.empty()) {
        return nullptr;
    }
    
    Backend* selected = healthy_backends[current_index_ % healthy_backends.size()];
    current_index_ = (current_index_ + 1) % healthy_backends.size();
    
    return selected;
}

Backend* LoadBalancer::random() {
    std::lock_guard<std::mutex> lock(mutex_);
    
    std::vector<Backend*> healthy_backends;
    for (auto& [id, backend] : backends_) {
        if (backend.healthy) {
            healthy_backends.push_back(&backend);
        }
    }
    
    if (healthy_backends.empty()) {
        return nullptr;
    }
    
    std::uniform_int_distribution<size_t> dist(0, healthy_backends.size() - 1);
    return healthy_backends[dist(rng_)];
}

Backend* LoadBalancer::least_connections() {
    std::lock_guard<std::mutex> lock(mutex_);
    
    Backend* selected = nullptr;
    uint32_t min_connections = UINT32_MAX;
    
    for (auto& [id, backend] : backends_) {
        if (backend.healthy && backend.active_connections < min_connections) {
            min_connections = backend.active_connections;
            selected = &backend;
        }
    }
    
    return selected;
}

Backend* LoadBalancer::weighted_round_robin() {
    std::lock_guard<std::mutex> lock(mutex_);
    
    std::vector<std::pair<Backend*, uint32_t>> weighted_backends;
    uint32_t total_weight = 0;
    
    for (auto& [id, backend] : backends_) {
        if (backend.healthy) {
            uint32_t weight = utils::normalize_weight(backend.weight);
            weighted_backends.push_back({&backend, weight});
            total_weight += weight;
        }
    }
    
    if (weighted_backends.empty()) {
        return nullptr;
    }
    
    std::vector<uint32_t> weights;
    for (const auto& [backend, weight] : weighted_backends) {
        weights.push_back(weight);
    }
    
    size_t index = utils::weighted_index(weights, total_weight);
    return weighted_backends[index].first;
}

Backend* LoadBalancer::ip_hash(const std::string& client_ip) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    std::vector<Backend*> healthy_backends;
    for (auto& [id, backend] : backends_) {
        if (backend.healthy) {
            healthy_backends.push_back(&backend);
        }
    }
    
    if (healthy_backends.empty()) {
        return nullptr;
    }
    
    uint32_t hash = utils::hash_ip(client_ip);
    size_t index = hash % healthy_backends.size();
    
    return healthy_backends[index];
}

Backend* LoadBalancer::consistent_hash(const std::string& key) {
    return ip_hash(key);
}

Backend* LoadBalancer::least_response_time() {
    std::lock_guard<std::mutex> lock(mutex_);
    
    Backend* selected = nullptr;
    std::chrono::microseconds min_time(UINT64_MAX);
    
    for (auto& [id, backend] : backends_) {
        if (backend.healthy && backend.avg_response_time < min_time) {
            min_time = backend.avg_response_time;
            selected = &backend;
        }
    }
    
    return selected;
}

void LoadBalancer::update_backend(const std::string& id, const std::chrono::microseconds& response_time) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto it = backends_.find(id);
    if (it != backends_.end()) {
        it->second.total_requests++;
        
        if (it->second.avg_response_time.count() == 0) {
            it->second.avg_response_time = response_time;
        } else {
            double alpha = 0.2;
            auto current = it->second.avg_response_time;
            it->second.avg_response_time = std::chrono::microseconds(
                static_cast<int64_t>(current.count() * (1 - alpha) + response_time.count() * alpha)
            );
        }
    }
}

void LoadBalancer::set_backend_health(const std::string& id, bool healthy) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto it = backends_.find(id);
    if (it != backends_.end()) {
        it->second.healthy = healthy;
    }
}

std::vector<Backend> LoadBalancer::backends() const {
    std::lock_guard<std::mutex> lock(mutex_);
    
    std::vector<Backend> result;
    for (const auto& [id, backend] : backends_) {
        result.push_back(backend);
    }
    return result;
}

std::vector<Backend> LoadBalancer::healthy_backends() const {
    std::lock_guard<std::mutex> lock(mutex_);
    
    std::vector<Backend> result;
    for (const auto& [id, backend] : backends_) {
        if (backend.healthy) {
            result.push_back(backend);
        }
    }
    return result;
}

void LoadBalancer::set_strategy(Strategy strategy) {
    std::lock_guard<std::mutex> lock(mutex_);
    strategy_ = strategy;
}

LoadBalancer::Statistics LoadBalancer::statistics() const {
    std::lock_guard<std::mutex> lock(mutex_);
    
    Statistics stats;
    size_t healthy_count = 0;
    std::chrono::microseconds total_time(0);
    
    for (const auto& [id, backend] : backends_) {
        stats.total_requests += backend.total_requests;
        if (backend.healthy) {
            healthy_count++;
            total_time += backend.avg_response_time;
            stats.active_connections += backend.active_connections;
        }
    }
    
    stats.healthy_backends = healthy_count;
    
    if (healthy_count > 0) {
        stats.avg_response_time = total_time / healthy_count;
    }
    
    return stats;
}

// ServiceLoadBalancer implementation
ServiceLoadBalancer::ServiceLoadBalancer(service_discovery::ServiceDiscovery::Ptr discovery,
                                        health::HealthChecker::Ptr health_checker,
                                        Strategy strategy)
    : discovery_(discovery)
    , health_checker_(health_checker)
    , default_strategy_(strategy)
{
}

ServiceLoadBalancer::Ptr ServiceLoadBalancer::create(service_discovery::ServiceDiscovery::Ptr discovery,
                                                     health::HealthChecker::Ptr health_checker,
                                                     Strategy strategy) {
    return std::shared_ptr<ServiceLoadBalancer>(new ServiceLoadBalancer(discovery, health_checker, strategy));
}

future::Future<Backend*> ServiceLoadBalancer::get_backend(const std::string& service_name,
                                                         const std::string& client_ip) {
    LoadBalancer::Ptr balancer;
    
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = service_balancers_.find(service_name);
        if (it != service_balancers_.end()) {
            balancer = it->second;
        }
    }
    
    if (!balancer) {
        auto strategy = default_strategy_;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            auto it = service_strategies_.find(service_name);
            if (it != service_strategies_.end()) {
                strategy = it->second;
            }
        }
        
        balancer = LoadBalancer::create(strategy);
        
        {
            std::lock_guard<std::mutex> lock(mutex_);
            service_balancers_[service_name] = balancer;
        }
        
        (void)refresh_backends(service_name);
    }
    
    Backend* backend = balancer->next_backend(client_ip);
    return future::make_ready_future<Backend*>(std::move(backend));
}

void ServiceLoadBalancer::update_backend(const std::string& service_name, const std::string& backend_id,
                                        const std::chrono::microseconds& response_time, bool success) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto it = service_balancers_.find(service_name);
    if (it != service_balancers_.end()) {
        it->second->update_backend(backend_id, response_time);
        
        if (!success) {
            it->second->set_backend_health(backend_id, false);
        }
    }
}

void ServiceLoadBalancer::set_strategy(const std::string& service_name, Strategy strategy) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    service_strategies_[service_name] = strategy;
    
    auto it = service_balancers_.find(service_name);
    if (it != service_balancers_.end()) {
        it->second->set_strategy(strategy);
    }
}

LoadBalancer::Statistics ServiceLoadBalancer::service_statistics(const std::string& service_name) const {
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto it = service_balancers_.find(service_name);
    if (it != service_balancers_.end()) {
        return it->second->statistics();
    }
    
    return LoadBalancer::Statistics{};
}

future::Future<void> ServiceLoadBalancer::refresh_backends(const std::string& service_name) {
    auto instances_future = discovery_->discover(service_name);
    
    std::vector<service_discovery::ServiceInstance> instances;
    if (instances_future.is_ready()) {
        instances = instances_future.get();
    }
    
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto it = service_balancers_.find(service_name);
    if (it == service_balancers_.end()) {
        return future::make_ready_future();
    }
    
    auto balancer = it->second;
    
    auto current_backends = balancer->backends();
    std::unordered_map<std::string, Backend> current_map;
    for (const auto& backend : current_backends) {
        current_map[backend.id] = backend;
    }
    
    for (const auto& instance : instances) {
        if (current_map.find(instance.id) == current_map.end()) {
            Backend backend;
            backend.id = instance.id;
            backend.address = instance.address;
            backend.port = instance.port;
            backend.healthy = instance.healthy;
            
            balancer->add_backend(backend);
        }
    }
    
    std::unordered_map<std::string, bool> instance_map;
    for (const auto& instance : instances) {
        instance_map[instance.id] = true;
    }
    
    for (const auto& [id, _] : current_map) {
        if (instance_map.find(id) == instance_map.end()) {
            balancer->remove_backend(id);
        }
    }
    
    return future::make_ready_future();
}

} // namespace load_balancer
} // namespace best_server