// Debug HTTP Server Test
// With error reporting

#include <best_server/best_server.hpp>
#include <iostream>
#include <csignal>
#include <atomic>
#include <cstring>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>

using namespace best_server;
using namespace best_server::network;

std::atomic<bool> running(true);

void signal_handler(int) {
    running = false;
}

int main() {
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    
    std::cout << "=== Best_Server Debug Test ===" << std::endl;
    
    // Test 1: Try to create a simple socket
    std::cout << "\nTest 1: Creating socket..." << std::endl;
    int test_fd = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK, 0);
    if (test_fd < 0) {
        std::cerr << "ERROR: socket() failed: " << strerror(errno) << std::endl;
        return 1;
    }
    std::cout << "✓ Socket created (fd=" << test_fd << ")" << std::endl;
    
    // Test 2: Try to bind to 127.0.0.1:8080
    std::cout << "\nTest 2: Binding to 127.0.0.1:8080..." << std::endl;
    struct sockaddr_in addr;
    std::memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(8080);
    inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);
    
    int optval = 1;
    setsockopt(test_fd, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval));
    
    if (bind(test_fd, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) < 0) {
        std::cerr << "ERROR: bind() failed: " << strerror(errno) << std::endl;
        close(test_fd);
        return 1;
    }
    std::cout << "✓ Bind successful" << std::endl;
    
    // Test 3: Try to listen
    std::cout << "\nTest 3: Listening..." << std::endl;
    if (listen(test_fd, 128) < 0) {
        std::cerr << "ERROR: listen() failed: " << strerror(errno) << std::endl;
        close(test_fd);
        return 1;
    }
    std::cout << "✓ Listen successful" << std::endl;
    
    close(test_fd);
    
    // Test 4: Try to start HTTPServer
    std::cout << "\nTest 4: Starting HTTPServer..." << std::endl;
    
    HTTPServer server;
    
    HTTPServerConfig config;
    config.address = "127.0.0.1";
    config.port = 8080;
    config.enable_http2 = false;
    config.enable_websocket = false;
    config.enable_compression = false;
    config.max_connections = 10;
    
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
    
    if (!server.start(config)) {
        std::cerr << "\nERROR: HTTPServer::start() failed!" << std::endl;
        std::cerr << "Possible issues:" << std::endl;
        std::cerr << "  1. Event loop not initialized" << std::endl;
        std::cerr << "  2. Worker threads failed to start" << std::endl;
        std::cerr << "  3. Acceptor bind failed (but we just tested bind works)" << std::endl;
        std::cerr << "  4. Missing dependencies" << std::endl;
        return 1;
    }
    
    std::cout << "\n✓✓✓ HTTPServer started successfully!" << std::endl;
    std::cout << "Address: http://" << config.address << ":" << config.port << std::endl;
    std::cout << "PID: " << getpid() << std::endl;
    std::cout << "\nTest in another terminal:" << std::endl;
    std::cout << "  curl http://127.0.0.1:8080/" << std::endl;
    std::cout << "  curl http://127.0.0.1:8080/api/test" << std::endl;
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