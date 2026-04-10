// TCPSocket - High-performance TCP socket with zero-copy support
// 
// Provides optimized TCP operations with:
// - Zero-copy I/O
// - Connection pooling
// - TLS support (optional)
// - TCP Fast Open
// - Keep-alive management

#ifndef BEST_SERVER_IO_TCP_SOCKET_HPP
#define BEST_SERVER_IO_TCP_SOCKET_HPP

#include <string>
#include <memory>
#include <functional>
#include <queue>
#include <array>
#include <cstdint>
#include <atomic>
#include <system_error>
#include <netinet/in.h>
#include <netinet/tcp.h>

#include "best_server/io/io_event_loop.hpp"
#include "best_server/memory/zero_copy_buffer.hpp"
#include "best_server/future/future.hpp"

namespace best_server {
namespace io {

// Forward declarations
class IOEventLoop;

namespace network {
class HTTPServer;
}

// Socket address
class SocketAddress {
public:
    SocketAddress();
    explicit SocketAddress(const std::string& ip, uint16_t port);
    
    std::string ip() const { return ip_; }
    uint16_t port() const { return port_; }
    std::string to_string() const;
    
    bool is_valid() const { return valid_; }
    
private:
    std::string ip_;
    uint16_t port_;
    bool valid_;
};

// Connection statistics
struct ConnectionStats {
    uint64_t bytes_sent{0};
    uint64_t bytes_received{0};
    uint64_t packets_sent{0};
    uint64_t packets_received{0};
    uint64_t reconnect_count{0};
};

// TCP socket
class TCPSocket {
public:
    // Pointer type
    using Ptr = std::shared_ptr<TCPSocket>;
    
    // Create a new socket
    static Ptr create() {
        return std::make_shared<TCPSocket>();
    }
    
    // Connection callback
    using ConnectCallback = std::function<void(std::error_code)>;
    
    // Receive callback
    using ReceiveCallback = std::function<void(memory::ZeroCopyBuffer&&, std::error_code)>;
    
    // Send callback
    using SendCallback = std::function<void(size_t, std::error_code)>;
    
    TCPSocket();
    ~TCPSocket();
    
    // Connect to a remote address
    void connect(const SocketAddress& address, ConnectCallback callback);
    
    // Connect with timeout
    void connect(const SocketAddress& address, ConnectCallback callback, uint32_t timeout_ms);
    
    // Send data
    void send(memory::ZeroCopyBuffer&& buffer, SendCallback callback);
    
    // Send data with scatter/gather
    void send(const std::vector<memory::ZeroCopyBuffer>& buffers, SendCallback callback);
    
    // Receive data
    void receive(size_t size, ReceiveCallback callback);
    
    // Receive into buffer
    void receive(memory::ZeroCopyBuffer& buffer, ReceiveCallback callback);
    
    // Close the connection
    void close();
    
    // Check if connected
    bool is_connected() const { return connected_; }
    
    // Get local address
    SocketAddress local_address() const;
    
    // Get remote address
    SocketAddress remote_address() const;
    
    // Get statistics
    const ConnectionStats& stats() const { return stats_; }
    
    // Set socket options
    void set_keep_alive(bool enable);
    void set_tcp_no_delay(bool enable);
    void set_send_buffer_size(size_t size);
    void set_receive_buffer_size(size_t size);
    
    // Enable TCP Fast Open
    void enable_fast_open(bool enable);
    
    // Set event loop
    void set_event_loop(IOEventLoop* loop) { event_loop_ = loop; }
    
    // Get native file descriptor
    int native_handle() const { 
        std::lock_guard<std::mutex> lock(close_mutex_);
        return fd_; 
    }
    
    // Wait for socket to be readable
    future::Future<void> wait_readable();
    
    // Wait for socket to be writable
    future::Future<void> wait_writable();
    
    // Sync connect method for convenience
    bool connect_sync(const std::string& host, uint16_t port, uint32_t timeout_ms = 30000);
    
    // Async read/write methods
    future::Future<memory::ZeroCopyBuffer> read_async(size_t size);
    future::Future<size_t> write_async(const std::vector<uint8_t>& data);
    future::Future<size_t> write_async(const memory::ZeroCopyBuffer& data);
    
    // Public method to handle incoming data (for event loop callback)
    void handle_incoming_data();
    
private:
    void handle_connect_event(EventType events);
    void handle_read_event(EventType events);
    void handle_write_event(EventType events);
    
    int fd_;
    bool connected_;
    bool closing_;  // 关闭标志，防止重复关闭
    SocketAddress remote_address_;
    SocketAddress local_address_;
    
    IOEventLoop* event_loop_;
    
    ReceiveCallback receive_callback_;
    SendCallback send_callback_;
    ConnectCallback connect_callback_;
    
    ConnectionStats stats_;
    
    // Send queue
    std::queue<memory::ZeroCopyBuffer> send_queue_;
    bool sending_;
    
    // Mutex to protect close() method from concurrent access
    mutable std::mutex close_mutex_;
    
    // 引用计数器，用于跟踪正在处理的回调
    std::atomic<int> callback_count_;
    
    friend class TCPAcceptor;
};

// TCP acceptor (server socket)
class TCPAcceptor {
public:
    using AcceptCallback = std::function<void(std::shared_ptr<TCPSocket>, std::error_code)>;
    
    TCPAcceptor();
    ~TCPAcceptor();
    
    // Bind and listen
    bool bind(const SocketAddress& address, int backlog = 128);
    
    // Start accepting connections
    void accept(AcceptCallback callback);
    
    // Stop accepting
    void stop();
    
    // Get bound address
    SocketAddress address() const;
    
    // Set event loop
    void set_event_loop(IOEventLoop* loop) { event_loop_ = loop; }
    
    // Sync connect method for convenience
    bool connect_sync(const std::string& host, uint16_t port, uint32_t timeout_ms = 30000);
    
private:
    void handle_accept_event(EventType events);
    
    int fd_;
    SocketAddress address_;
    IOEventLoop* event_loop_;
    AcceptCallback accept_callback_;
};

// Connection pool
class TCPConnectionPool {
public:
    using ConnectionCallback = std::function<void(std::shared_ptr<TCPSocket>, std::error_code)>;
    
    TCPConnectionPool(size_t max_connections = 100);
    ~TCPConnectionPool();
    
    // Get a connection from the pool
    void get_connection(const SocketAddress& address, ConnectionCallback callback);
    
    // Return a connection to the pool
    void return_connection(std::shared_ptr<TCPSocket> socket);
    
    // Set event loop
    void set_event_loop(IOEventLoop* loop) { event_loop_ = loop; }
    
    // Get pool statistics
    size_t active_connections() const;
    size_t idle_connections() const;
    
private:
    IOEventLoop* event_loop_;
    size_t max_connections_;
    
    std::unordered_map<std::string, std::vector<std::shared_ptr<TCPSocket>>> pools_;
    mutable std::mutex mutex_;
};

} // namespace io
} // namespace best_server

#endif // BEST_SERVER_IO_TCP_SOCKET_HPP