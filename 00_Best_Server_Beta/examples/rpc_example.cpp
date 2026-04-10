// RPC Server and Client Example
// 
// This example demonstrates how to create an RPC server and client
// using the Best_Server framework.

#include <best_server/best_server.hpp>
#include <iostream>
#include <thread>
#include <chrono>

using namespace best_server;
using namespace best_server::rpc;

// Example RPC service
class CalculatorService : public RPCService {
public:
    CalculatorService() : RPCService("calculator") {
        // Register methods
        register_method("add", [this](const RPCRequest& req) {
            return handle_add(req);
        });
        
        register_method("subtract", [this](const RPCRequest& req) {
            return handle_subtract(req);
        });
        
        register_method("multiply", [this](const RPCRequest& req) {
            return handle_multiply(req);
        });
        
        register_method("divide", [this](const RPCRequest& req) {
            return handle_divide(req);
        });
    }
    
private:
    RPCResponse handle_add(const RPCRequest& req) {
        RPCResponse res;
        res.set_request_id(req.request_id());
        
        // Parse parameters (in real implementation, use proper serialization)
        int a = req.get_param<int>("a");
        int b = req.get_param<int>("b");
        
        int result = a + b;
        res.set_result(result);
        
        return res;
    }
    
    RPCResponse handle_subtract(const RPCRequest& req) {
        RPCResponse res;
        res.set_request_id(req.request_id());
        
        int a = req.get_param<int>("a");
        int b = req.get_param<int>("b");
        
        int result = a - b;
        res.set_result(result);
        
        return res;
    }
    
    RPCResponse handle_multiply(const RPCRequest& req) {
        RPCResponse res;
        res.set_request_id(req.request_id());
        
        int a = req.get_param<int>("a");
        int b = req.get_param<int>("b");
        
        int result = a * b;
        res.set_result(result);
        
        return res;
    }
    
    RPCResponse handle_divide(const RPCRequest& req) {
        RPCResponse res;
        res.set_request_id(req.request_id());
        
        int a = req.get_param<int>("a");
        int b = req.get_param<int>("b");
        
        if (b == 0) {
            res.set_error(400, "Division by zero");
            return res;
        }
        
        int result = a / b;
        res.set_result(result);
        
        return res;
    }
};

void run_server() {
    std::cout << "Starting RPC Server..." << std::endl;
    
    // Create RPC server
    RPCServer server;
    
    // Configure server
    RPCServerConfig config;
    config.address = "0.0.0.0";
    config.port = 9000;
    config.enable_compression = true;
    
    // Register service
    auto calculator = std::make_shared<CalculatorService>();
    server.register_service(calculator);
    
    // Start server
    if (!server.start(config)) {
        std::cerr << "Failed to start server" << std::endl;
        return;
    }
    
    std::cout << "RPC Server started on " << config.address << ":" << config.port << std::endl;
    
    // Keep server running
    while (true) {
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }
}

void run_client() {
    std::cout << "Starting RPC Client..." << std::endl;
    
    // Wait for server to start
    std::this_thread::sleep_for(std::chrono::seconds(1));
    
    // Create RPC client
    RPCClientConfig config;
    config.server_address = "127.0.0.1";
    config.server_port = 9000;
    
    RPCClient client(config);
    
    // Connect to server
    if (!client.connect()) {
        std::cerr << "Failed to connect to server" << std::endl;
        return;
    }
    
    std::cout << "Connected to server" << std::endl;
    
    // Call methods
    {
        RPCRequest req;
        req.set_service("calculator");
        req.set_method("add");
        req.set_param("a", 5);
        req.set_param("b", 3);
        
        auto res = client.call("calculator", "add", req);
        if (!res.has_error()) {
            std::cout << "5 + 3 = " << res.get_result<int>() << std::endl;
        } else {
            std::cout << "Error: " << res.error_message() << std::endl;
        }
    }
    
    {
        RPCRequest req;
        req.set_service("calculator");
        req.set_method("multiply");
        req.set_param("a", 4);
        req.set_param("b", 7);
        
        auto res = client.call("calculator", "multiply", req);
        if (!res.has_error()) {
            std::cout << "4 * 7 = " << res.get_result<int>() << std::endl;
        } else {
            std::cout << "Error: " << res.error_message() << std::endl;
        }
    }
    
    {
        RPCRequest req;
        req.set_service("calculator");
        req.set_method("divide");
        req.set_param("a", 10);
        req.set_param("b", 2);
        
        auto res = client.call("calculator", "divide", req);
        if (!res.has_error()) {
            std::cout << "10 / 2 = " << res.get_result<int>() << std::endl;
        } else {
            std::cout << "Error: " << res.error_message() << std::endl;
        }
    }
    
    // Async call
    {
        RPCRequest req;
        req.set_service("calculator");
        req.set_method("add");
        req.set_param("a", 100);
        req.set_param("b", 200);
        
        auto future = client.call_async("calculator", "add", req);
        auto res = future.get();
        if (!res.has_error()) {
            std::cout << "100 + 200 = " << res.get_result<int>() << std::endl;
        }
    }
    
    // Print statistics
    auto stats = client.stats();
    std::cout << "\nClient Statistics:" << std::endl;
    std::cout << "  Total requests: " << stats.total_requests << std::endl;
    std::cout << "  Successful: " << stats.successful_requests << std::endl;
    std::cout << "  Failed: " << stats.failed_requests << std::endl;
    std::cout << "  Average latency: " << stats.average_latency_us << " us" << std::endl;
    
    client.disconnect();
}

int main(int argc, char* argv[]) {
    std::cout << "Best_Server - RPC Example" << std::endl;
    std::cout << "=========================" << std::endl;
    
    if (argc > 1 && std::string(argv[1]) == "server") {
        run_server();
    } else if (argc > 1 && std::string(argv[1]) == "client") {
        run_client();
    } else {
        std::cout << "Usage: " << argv[0] << " [server|client]" << std::endl;
        std::cout << "\nStarting both server and client..." << std::endl;
        
        // Run server in background
        std::thread server_thread(run_server);
        
        // Run client
        run_client();
        
        // Stop server
        server_thread.detach();
    }
    
    return 0;
}