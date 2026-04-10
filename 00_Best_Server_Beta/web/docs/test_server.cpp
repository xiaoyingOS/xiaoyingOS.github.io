// Simple HTTP Server Test
// Minimal version to verify the framework works

#include <best_server/best_server.hpp>
#include <iostream>
#include <csignal>
#include <atomic>

using namespace best_server;
using namespace best_server::network;

std::atomic<bool> running(true);

void signal_handler(int) {
    running = false;
}

int main() {
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    
    std::cout << "Starting Best_Server Test..." << std::endl;
    
    HTTPServer server;
    
    HTTPServerConfig config;
    config.address = "127.0.0.1";
    config.port = 8080;
    config.enable_http2 = false;
    config.enable_websocket = false;
    config.enable_compression = false;
    
    // Add a simple route
    server.get("/", [](HTTPRequest& req, HTTPResponse& res) {
        (void)req;
        res.set_status(HTTPStatus::OK);
        res.set_text("Best_Server is working!");
    });
    
    server.get("/api/test", [](HTTPRequest& req, HTTPResponse& res) {
        (void)req;
        res.set_status(HTTPStatus::OK);
        res.set_json(R"({"status": "ok", "message": "API is working"})");
    });
    
    std::cout << "Starting server..." << std::endl;
    if (!server.start(config)) {
        std::cerr << "ERROR: Failed to start server!" << std::endl;
        return 1;
    }
    
    std::cout << "✓ Server started on " << config.address << ":" << config.port << std::endl;
    std::cout << "✓ PID: " << getpid() << std::endl;
    std::cout << "\nTest endpoints:" << std::endl;
    std::cout << "  http://127.0.0.1:8080/" << std::endl;
    std::cout << "  http://127.0.0.1:8080/api/test" << std::endl;
    std::cout << "\nPress Ctrl+C to stop..." << std::endl;
    
    // Keep running
    while (running) {
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }
    
    std::cout << "\nStopping server..." << std::endl;
    server.stop();
    std::cout << "✓ Server stopped" << std::endl;
    
    return 0;
}