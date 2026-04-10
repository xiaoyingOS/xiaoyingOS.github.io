// RPCServer implementation
#include "best_server/rpc/rpc_server.hpp"
#include "best_server/rpc/rpc_protocol.hpp"
#include <algorithm>
#include <sstream>

namespace best_server {
namespace rpc {

// RPCServer implementation
RPCServer::RPCServer()
    : next_message_id_(1)
    , running_(false)
{
    protocol_ = std::make_shared<RPCProtocol>();
}

RPCServer::~RPCServer() {
    stop();
}

bool RPCServer::start(const std::string& address, uint16_t port) {
    if (running_) {
        return false;
    }
    
    address_ = address;
    port_ = port;
    
    running_ = true;
    return true;
}

void RPCServer::stop() {
    if (!running_) {
        return;
    }
    
    running_ = false;
    
    // Close all connections
    std::lock_guard<std::mutex> lock(connections_mutex_);
    connections_.clear();
}

void RPCServer::register_handler(const std::string& method, RPCHandler handler) {
    std::lock_guard<std::mutex> lock(handlers_mutex_);
    handlers_[method] = handler;
}

void RPCServer::unregister_handler(const std::string& method) {
    std::lock_guard<std::mutex> lock(handlers_mutex_);
    handlers_.erase(method);
}

void RPCServer::set_protocol(std::shared_ptr<RPCProtocol> protocol) {
    protocol_ = protocol;
}

void RPCServer::handle_request(const RPCRequest& request, RPCResponse& response) {
    response.id = request.id;
    
    // Find handler
    RPCHandler handler;
    {
        std::lock_guard<std::mutex> lock(handlers_mutex_);
        auto it = handlers_.find(request.method);
        if (it != handlers_.end()) {
            handler = it->second;
        }
    }
    
    if (!handler) {
        response.error = R"({"code":-32601,"message":"Method not found"})";
        return;
    }
    
    // Execute handler
    try {
        handler(request, response);
    } catch (const std::exception& e) {
        response.error = R"({"code":-32603,"message":"Internal error: ")" + std::string(e.what()) + R"("})";
    }
}

const ServerStats& RPCServer::stats() const {
    return stats_;
}

// RPCClient implementation
RPCClient::RPCClient()
    : next_message_id_(1)
    , connected_(false)
{
    protocol_ = std::make_shared<RPCProtocol>();
}

RPCClient::~RPCClient() {
    disconnect();
}

bool RPCClient::connect(const std::string& address, uint16_t port) {
    if (connected_) {
        return false;
    }
    
    address_ = address;
    port_ = port;
    
    // In a real implementation, you would establish a TCP connection here
    connected_ = true;
    return true;
}

void RPCClient::disconnect() {
    if (!connected_) {
        return;
    }
    
    connected_ = false;
    
    // Cancel all pending requests
    std::lock_guard<std::mutex> lock(pending_requests_mutex_);
    for (auto& [id, pending] : pending_requests_) {
        RPCResponse response;
        response.id = id;
        response.error = R"({"code":-32603,"message":"Client disconnected"})";
        pending.promise.set_value(response);
    }
    pending_requests_.clear();
}

void RPCClient::set_protocol(std::shared_ptr<RPCProtocol> protocol) {
    protocol_ = protocol;
}

future::Future<RPCResponse> RPCClient::call(const std::string& method, const std::string& params) {
    if (!connected_) {
        RPCResponse response;
        response.id = next_message_id_++;
        response.error = R"({"code":-32603,"message":"Not connected"})";
        return future::make_ready_future(std::move(response));
    }
    
    RPCRequest request;
    request.id = next_message_id_++;
    request.method = method;
    request.params = params;
    
    // Serialize request
    auto serialized = protocol_->serialize_request(request);
    
    // Send request (in a real implementation, you would send over the network)
    
    // Create promise for response
    PendingRequest pending;
    auto future = pending.promise.get_future();
    
    {
        std::lock_guard<std::mutex> lock(pending_requests_mutex_);
        pending_requests_[request.id] = std::move(pending);
    }
    
    return future;
}

void RPCClient::handle_response(const RPCResponse& response) {
    std::lock_guard<std::mutex> lock(pending_requests_mutex_);
    
    auto it = pending_requests_.find(response.id);
    if (it != pending_requests_.end()) {
        it->second.promise.set_value(response);
        pending_requests_.erase(it);
    }
}

void RPCClient::set_timeout(uint32_t timeout_ms) {
    timeout_ms_ = timeout_ms;
}

const ClientStats& RPCClient::stats() const {
    return stats_;
}

} // namespace rpc
} // namespace best_server