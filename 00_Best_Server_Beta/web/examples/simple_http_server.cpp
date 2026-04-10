// Simple HTTP Server Example
// 
// This example demonstrates how to create a simple HTTP server
// using the Best_Server framework.

#include <best_server/best_server.hpp>
#include <iostream>

using namespace best_server;
using namespace best_server::network;

int main() {
    std::cout << "Best_Server - Simple HTTP Server Example" << std::endl;
    std::cout << "=========================================" << std::endl;
    
    // Create HTTP server
    HTTPServer server;
    
    // Configure server
    HTTPServerConfig config;
    config.address = "0.0.0.0";
    config.port = 8080;
    config.enable_http2 = true;
    config.enable_compression = true;
    
    // Register route handlers
    server.get("/", [](HTTPRequest& req, HTTPResponse& res) {
        res.set_status(HTTPStatus::OK);
        res.set_html(R"(
            <!DOCTYPE html>
            <html>
            <head>
                <title>Best_Server</title>
            </head>
            <body>
                <h1>Welcome to Best_Server!</h1>
                <p>A high-performance multi-platform async server framework.</p>
                <ul>
                    <li><a href="/hello">Say Hello</a></li>
                    <li><a href="/time">Current Time</a></li>
                    <li><a href="/json">JSON Example</a></li>
                </ul>
            </body>
            </html>
        )");
    });
    
    server.get("/hello", [](HTTPRequest& req, HTTPResponse& res) {
        res.set_status(HTTPStatus::OK);
        res.set_text("Hello, World!");
    });
    
    server.get("/time", [](HTTPRequest& req, HTTPResponse& res) {
        auto now = std::chrono::system_clock::now();
        auto time = std::chrono::system_clock::to_time_t(now);
        res.set_status(HTTPStatus::OK);
        res.set_text(std::string("Current time: ") + std::ctime(&time));
    });
    
    server.get("/json", [](HTTPRequest& req, HTTPResponse& res) {
        res.set_status(HTTPStatus::OK);
        res.set_json(R"({
            "message": "Hello from Best_Server!",
            "version": "1.0.0",
            "features": [
                "Zero-copy I/O",
                "Per-core sharding",
                "Async I/O",
                "HTTP/2 support",
                "RPC framework"
            ]
        })");
    });
    
    server.post("/echo", [](HTTPRequest& req, HTTPResponse& res) {
        res.set_status(HTTPStatus::OK);
        res.set_content_type(req.content_type());
        res.set_body(req.body());
    });
    
    // Start server
    if (!server.start(config)) {
        std::cerr << "Failed to start server" << std::endl;
        return 1;
    }
    
    std::cout << "Server started on " << config.address << ":" << config.port << std::endl;
    std::cout << "Press Ctrl+C to stop..." << std::endl;
    
    // Wait for user input
    std::cin.get();
    
    // Stop server
    server.stop();
    
    std::cout << "Server stopped" << std::endl;
    
    return 0;
}