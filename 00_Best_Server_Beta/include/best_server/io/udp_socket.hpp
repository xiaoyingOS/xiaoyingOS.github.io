// UDPSocket - High-performance UDP socket with zero-copy support
// 
// Provides optimized UDP operations with:
// - Zero-copy I/O
// - Multicast support
// - Broadcast support
// - UDP Fast Path
// - Message batching

#ifndef BEST_SERVER_IO_UDP_SOCKET_HPP
#define BEST_SERVER_IO_UDP_SOCKET_HPP

#include <string>
#include <memory>
#include <functional>
#include <system_error>
#include <vector>

#include "best_server/io/io_event_loop.hpp"
#include "best_server/memory/zero_copy_buffer.hpp"
#include "best_server/io/tcp_socket.hpp"

namespace best_server {
namespace io {

// UDP message
struct UDPMessage {
    SocketAddress sender;
    memory::ZeroCopyBuffer data;
    uint64_t timestamp;
};

// UDP socket statistics
struct UDPStats {
    uint64_t packets_sent{0};
    uint64_t packets_received{0};
    uint64_t bytes_sent{0};
    uint64_t bytes_received{0};
    uint64_t packets_dropped{0};
};

// Receive callback
using UDPReceiveCallback = std::function<void(UDPMessage&&, std::error_code)>;

// Send callback
using UDPSendCallback = std::function<void(size_t, std::error_code)>;

// UDP socket
class UDPSocket {
public:
    UDPSocket();
    ~UDPSocket();
    
    // Bind to a local address
    bool bind(const SocketAddress& address);
    
    // Bind to any address
    bool bind(uint16_t port);
    
    // Send a message
    void send_to(const SocketAddress& destination, 
                 memory::ZeroCopyBuffer&& data,
                 UDPSendCallback callback);
    
    // Send multiple messages (batch)
    void send_to_batch(const std::vector<std::pair<SocketAddress, memory::ZeroCopyBuffer>>& messages,
                       UDPSendCallback callback);
    
    // Receive a message
    void receive(UDPReceiveCallback callback);
    
    // Join a multicast group
    bool join_multicast(const std::string& multicast_address);
    
    // Leave a multicast group
    bool leave_multicast(const std::string& multicast_address);
    
    // Enable broadcast
    void set_broadcast(bool enable);
    
    // Set receive buffer size
    void set_receive_buffer_size(size_t size);
    
    // Set send buffer size
    void set_send_buffer_size(size_t size);
    
    // Close the socket
    void close();
    
    // Get local address
    SocketAddress local_address() const;
    
    // Get statistics
    const UDPStats& stats() const { return stats_; }
    
    // Set event loop
    void set_event_loop(IOEventLoop* loop) { event_loop_ = loop; }
    
private:
    void handle_read_event(EventType events);
    void handle_write_event(EventType events);
    
    int fd_;
    SocketAddress local_address_;
    IOEventLoop* event_loop_;
    
    UDPReceiveCallback receive_callback_;
    std::vector<std::function<void()>> send_callbacks_;
    
    UDPStats stats_;
    bool receiving_;
};

// UDP server (for high-performance datagram processing)
class UDPServer {
public:
    using MessageHandler = std::function<void(UDPMessage&&, UDPSocket*)>;
    
    UDPServer();
    ~UDPServer();
    
    // Start the server
    bool start(const SocketAddress& address, MessageHandler handler);
    
    // Start on a specific port
    bool start(uint16_t port, MessageHandler handler);
    
    // Stop the server
    void stop();
    
    // Get the underlying socket
    UDPSocket* socket() { return socket_.get(); }
    
    // Set event loop
    void set_event_loop(IOEventLoop* loop) { event_loop_ = loop; }
    
private:
    std::unique_ptr<UDPSocket> socket_;
    MessageHandler handler_;
    IOEventLoop* event_loop_;
    bool running_;
};

// UDP client pool (for multiple senders)
class UDPClientPool {
public:
    UDPClientPool(size_t pool_size = 10);
    ~UDPClientPool();
    
    // Get a socket from the pool
    UDPSocket* acquire();
    
    // Return a socket to the pool
    void release(UDPSocket* socket);
    
    // Bind all sockets to a local address
    bool bind_all(const SocketAddress& address);
    
    // Set event loop
    void set_event_loop(IOEventLoop* loop) { event_loop_ = loop; }
    
private:
    IOEventLoop* event_loop_;
    std::vector<std::unique_ptr<UDPSocket>> sockets_;
    std::queue<UDPSocket*> available_;
    std::mutex mutex_;
};

} // namespace io
} // namespace best_server

#endif // BEST_SERVER_IO_UDP_SOCKET_HPP