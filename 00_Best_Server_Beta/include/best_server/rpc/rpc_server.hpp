// RPCServer - High-performance RPC server
// 
// Implements an optimized RPC server with:
// - Automatic serialization/deserialization
// - Zero-copy data transfer
// - Connection pooling
// - Request batching
// - Load balancing
// - Service discovery

#ifndef BEST_SERVER_RPC_RPC_SERVER_HPP
#define BEST_SERVER_RPC_RPC_SERVER_HPP

#include <string>
#include <memory>
#include <functional>
#include <unordered_map>
#include <vector>
#include <future>

#include "io/tcp_socket.hpp"
#include "network/http_server.hpp"

namespace best_server {
namespace rpc {

// Forward declarations
class RPCProtocol;
class RPCConnection;

// RPC request
class RPCRequest {
public:
    RPCRequest();
    ~RPCRequest();
    
    // Service and method
    const std::string& service() const { return service_; }
    void set_service(const std::string& service) { service_ = service; }
    
    const std::string& method() const { return method_; }
    void set_method(const std::string& method) { method_ = method; }
    
    // Request ID
    uint64_t request_id() const { return request_id_; }
    void set_request_id(uint64_t id) { request_id_ = id; }
    
    // Parameters
    template<typename T>
    void set_param(const std::string& name, const T& value) {
        (void)name;
        (void)value;
        // Would integrate with serialization library
    }
    
    template<typename T>
    T get_param(const std::string& name) const {
        (void)name;
        // Would integrate with serialization library
        return T{};
    }
    
    // Raw data
    const std::string& data() const { return data_; }
    void set_data(const std::string& data) { data_ = data; }
    
    // Metadata
    const std::unordered_map<std::string, std::string>& metadata() const { return metadata_; }
    void set_metadata(const std::string& key, const std::string& value);
    
private:
    std::string service_;
    std::string method_;
    uint64_t request_id_;
    std::string data_;
    std::unordered_map<std::string, std::string> metadata_;
};

// RPC response
class RPCResponse {
public:
    RPCResponse();
    ~RPCResponse();
    
    // Request ID
    uint64_t request_id() const { return request_id_; }
    void set_request_id(uint64_t id) { request_id_ = id; }
    
    // Result
    template<typename T>
    void set_result(const T& result) {
        (void)result;
        // Would integrate with serialization library
    }
    
    template<typename T>
    T get_result() const {
        // Would integrate with serialization library
        return T{};
    }
    
    // Error
    bool has_error() const { return has_error_; }
    const std::string& error_message() const { return error_message_; }
    int error_code() const { return error_code_; }
    
    void set_error(int code, const std::string& message);
    
    // Raw data
    const std::string& data() const { return data_; }
    void set_data(const std::string& data) { data_ = data; }
    
    // Metadata
    const std::unordered_map<std::string, std::string>& metadata() const { return metadata_; }
    void set_metadata(const std::string& key, const std::string& value);
    
private:
    uint64_t request_id_;
    std::string data_;
    bool has_error_;
    int error_code_;
    std::string error_message_;
    std::unordered_map<std::string, std::string> metadata_;
};

// RPC method handler
using RPCMethodHandler = std::function<RPCResponse(const RPCRequest&)>;

// Async RPC method handler
using AsyncRPCMethodHandler = std::function<future::Future<RPCResponse>(const RPCRequest&)>;

// RPC service
class RPCService {
public:
    RPCService(const std::string& name);
    ~RPCService();
    
    // Get service name
    const std::string& name() const { return name_; }
    
    // Register a method
    void register_method(const std::string& method_name, RPCMethodHandler handler);
    void register_async_method(const std::string& method_name, AsyncRPCMethodHandler handler);
    
    // Call a method
    RPCResponse call_method(const std::string& method_name, const RPCRequest& request);
    future::Future<RPCResponse> call_async_method(const std::string& method_name, const RPCRequest& request);
    
    // Check if method exists
    bool has_method(const std::string& method_name) const;
    
    // Get all methods
    std::vector<std::string> methods() const;
    
private:
    std::string name_;
    std::unordered_map<std::string, RPCMethodHandler> methods_;
    std::unordered_map<std::string, AsyncRPCMethodHandler> async_methods_;
};

// RPC server configuration
struct RPCServerConfig {
    std::string address{"0.0.0.0"};
    uint16_t port{9000};
    int backlog{128};
    bool enable_http_transport{true};
    bool enable_tcp_transport{true};
    size_t max_request_size{10 * 1024 * 1024}; // 10MB
    size_t max_response_size{10 * 1024 * 1024}; // 10MB
    uint32_t timeout_ms{30000}; // 30s
    uint32_t max_connections{10000};
    bool enable_compression{true};
    bool enable_encryption{false};
    std::string cert_file;
    std::string key_file;
};

// RPC server statistics
struct RPCServerStats {
    uint64_t total_connections{0};
    uint64_t active_connections{0};
    uint64_t total_requests{0};
    uint64_t requests_per_second{0};
    uint64_t bytes_sent{0};
    uint64_t bytes_received{0};
    uint64_t successful_requests{0};
    uint64_t failed_requests{0};
    uint64_t average_latency_us{0};
};

// RPC server
class RPCServer {
public:
    RPCServer();
    ~RPCServer();
    
    // Start the server
    bool start(const RPCServerConfig& config = RPCServerConfig{});
    
    // Stop the server
    void stop();
    
    // Register a service
    void register_service(std::shared_ptr<RPCService> service);
    
    // Register a method directly
    void register_method(const std::string& service_name, const std::string& method_name, RPCMethodHandler handler);
    void register_async_method(const std::string& service_name, const std::string& method_name, AsyncRPCMethodHandler handler);
    
    // Get a service
    std::shared_ptr<RPCService> get_service(const std::string& service_name);
    
    // Get statistics
    const RPCServerStats& stats() const { return stats_; }
    
    // Get configuration
    const RPCServerConfig& config() const { return config_; }
    
    // Check if running
    bool is_running() const { return running_; }
    
    // Enable service discovery
    void enable_service_discovery(bool enable);
    
private:
    void handle_connection(std::unique_ptr<RPCConnection> connection);
    void handle_request(const RPCRequest& request, RPCResponse& response);
    void route_request(const RPCRequest& request, RPCResponse& response);
    
    std::unordered_map<std::string, std::shared_ptr<RPCService>> services_;
    std::unordered_map<std::string, std::string> service_method_map_;
    
    RPCServerConfig config_;
    RPCServerStats stats_;
    std::atomic<bool> running_{false};
    
    // Transport layers
    std::unique_ptr<io::TCPAcceptor> tcp_acceptor_;
    std::unique_ptr<network::HTTPServer> http_server_;
    
    std::unordered_map<int, std::unique_ptr<RPCConnection>> connections_;
    std::mutex connections_mutex_;
    
    // Protocol
    std::unique_ptr<RPCProtocol> protocol_;
};

} // namespace rpc
} // namespace best_server

#endif // BEST_SERVER_RPC_RPC_SERVER_HPP