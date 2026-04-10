// RPCClient - High-performance RPC client
// 
// Implements an optimized RPC client with:
// - Connection pooling
// - Request pipelining
// - Automatic retries
// - Load balancing
// - Service discovery
// - Zero-copy data transfer

#ifndef BEST_SERVER_RPC_RPC_CLIENT_HPP
#define BEST_SERVER_RPC_RPC_CLIENT_HPP

#include <string>
#include <memory>
#include <functional>
#include <unordered_map>
#include <vector>
#include <future>

#include "io/tcp_socket.hpp"
#include "rpc/rpc_server.hpp"

namespace best_server {
namespace rpc {

// Forward declarations
class RPCProtocol;

// RPC client configuration
struct RPCClientConfig {
    std::string server_address{"127.0.0.1"};
    uint16_t server_port{9000};
    bool use_http_transport{false};
    size_t max_connections{10};
    size_t max_pending_requests{100};
    uint32_t timeout_ms{5000}; // 5s
    uint32_t connect_timeout_ms{3000}; // 3s
    bool enable_compression{true};
    bool enable_encryption{false};
    std::string cert_file;
    std::string key_file;
    uint32_t max_retries{3};
    uint32_t retry_delay_ms{100};
};

// RPC client statistics
struct RPCClientStats {
    uint64_t total_requests{0};
    uint64_t successful_requests{0};
    uint64_t failed_requests{0};
    uint64_t requests_per_second{0};
    uint64_t bytes_sent{0};
    uint64_t bytes_received{0};
    uint64_t active_connections{0};
    uint64_t average_latency_us{0};
    uint32_t pending_requests{0};
};

// RPC client
class RPCClient {
public:
    RPCClient();
    explicit RPCClient(const RPCClientConfig& config);
    ~RPCClient();
    
    // Configure client
    void configure(const RPCClientConfig& config);
    
    // Connect to server
    bool connect();
    future::Future<bool> connect_async();
    
    // Disconnect
    void disconnect();
    
    // Call a method (sync)
    RPCResponse call(const std::string& service, const std::string& method, const RPCRequest& request);
    template<typename... Args>
    RPCResponse call(const std::string& service, const std::string& method, Args&&... args) {
        [[maybe_unused]] auto _ = std::make_tuple(args...);
        RPCRequest req;
        req.set_service(service);
        req.set_method(method);
        // Pack args into request
        return call(service, method, req);
    }
    
    // Call a method (async)
    future::Future<RPCResponse> call_async(const std::string& service, const std::string& method, const RPCRequest& request);
    template<typename... Args>
    future::Future<RPCResponse> call_async(const std::string& service, const std::string& method, Args&&... args) {
        [[maybe_unused]] auto _ = std::make_tuple(args...);
        RPCRequest req;
        req.set_service(service);
        req.set_method(method);
        // Pack args into request
        return call_async(service, method, req);
    }
    
    // One-way call (fire and forget)
    void call_one_way(const std::string& service, const std::string& method, const RPCRequest& request);
    template<typename... Args>
    void call_one_way(const std::string& service, const std::string& method, Args&&... args) {
        [[maybe_unused]] auto _ = std::make_tuple(args...);
        RPCRequest req;
        req.set_service(service);
        req.set_method(method);
        // Pack args into request
        call_one_way(service, method, req);
    }
    
    // Batch calls
    std::vector<RPCResponse> call_batch(const std::vector<std::tuple<std::string, std::string, RPCRequest>>& requests);
    future::Future<std::vector<RPCResponse>> call_batch_async(const std::vector<std::tuple<std::string, std::string, RPCRequest>>& requests);
    
    // Get statistics
    RPCClientStats stats() const;
    
    // Get configuration
    const RPCClientConfig& config() const { return config_; }
    
    // Check if connected
    bool is_connected() const;
    
    // Enable connection pool
    void enable_connection_pool(bool enable, size_t pool_size = 10);
    
    // Set timeout
    void set_timeout(uint32_t timeout_ms);
    
    // Set retry policy
    void set_retry_policy(uint32_t max_retries, uint32_t retry_delay_ms);
    
    // Enable compression
    void enable_compression(bool enable);
    
    // Service discovery
    future::Future<std::vector<std::string>> discover_services();
    future::Future<std::vector<std::string>> discover_methods(const std::string& service);
    
private:
    // Connection management
    bool acquire_connection(io::TCPSocket*& connection);
    void release_connection(io::TCPSocket* connection);
    
    // Send request
    bool send_request(io::TCPSocket* connection, const RPCRequest& request);
    
    // Receive response
    bool receive_response(io::TCPSocket* connection, RPCResponse& response);
    
    // Retry logic
    RPCResponse call_with_retry(const std::string& service, const std::string& method, const RPCRequest& request);
    
    RPCClientConfig config_;
    RPCClientStats stats_;
    
    std::unique_ptr<RPCProtocol> protocol_;
    
    // Connection pool
    std::vector<std::unique_ptr<io::TCPSocket>> connections_;
    std::queue<io::TCPSocket*> available_connections_;
    std::mutex connections_mutex_;
    
    // Pending requests
    std::unordered_map<uint64_t, std::function<void(const RPCResponse&)>> pending_requests_;
    std::mutex pending_requests_mutex_;
    std::atomic<uint64_t> next_request_id_{1};
    
    // Event loop
    io::IOEventLoop* event_loop_;
    
    bool connected_;
    bool pool_enabled_;
};

// Load balanced RPC client
class LoadBalancedRPCClient {
public:
    LoadBalancedRPCClient();
    ~LoadBalancedRPCClient();
    
    // Add server
    void add_server(const std::string& address, uint16_t port);
    
    // Remove server
    void remove_server(const std::string& address, uint16_t port);
    
    // Call method (auto load balancing)
    RPCResponse call(const std::string& service, const std::string& method, const RPCRequest& request);
    future::Future<RPCResponse> call_async(const std::string& service, const std::string& method, const RPCRequest& request);
    
    // Load balancing strategies
    enum class LoadBalanceStrategy {
        RoundRobin,
        LeastConnections,
        Random,
        WeightedRoundRobin
    };
    void set_load_balance_strategy(LoadBalanceStrategy strategy);
    
    // Get server health
    struct ServerHealth {
        std::string address;
        uint16_t port;
        bool healthy;
        uint64_t request_count;
        uint64_t error_count;
        double success_rate;
        uint64_t average_latency_us;
    };
    std::vector<ServerHealth> get_server_health() const;
    
    // Enable health checks
    void enable_health_checks(bool enable, uint32_t interval_ms = 5000);
    
private:
    struct ServerInfo {
        std::string address;
        uint16_t port;
        std::unique_ptr<RPCClient> client;
        ServerHealth health;
        uint32_t weight;
    };
    
    ServerInfo* select_server();
    void health_check_thread();
    
    std::vector<std::unique_ptr<ServerInfo>> servers_;
    LoadBalanceStrategy strategy_;
    size_t current_server_index_;
    
    std::mutex servers_mutex_;
    std::atomic<bool> health_checks_enabled_{false};
    std::thread health_check_thread_;
    std::atomic<bool> running_{false};
};

// RPC client pool (for multiple independent clients)
class RPCClientPool {
public:
    RPCClientPool(size_t pool_size = 10);
    ~RPCClientPool();
    
    // Get a client
    RPCClient* acquire();
    
    // Return a client
    void release(RPCClient* client);
    
    // Configure all clients
    void configure(const RPCClientConfig& config);
    
    // Get pool statistics
    size_t active_clients() const;
    size_t idle_clients() const;
    
private:
    std::vector<std::unique_ptr<RPCClient>> clients_;
    std::queue<RPCClient*> available_;
    std::mutex mutex_;
};

} // namespace rpc
} // namespace best_server

#endif // BEST_SERVER_RPC_RPC_CLIENT_HPP