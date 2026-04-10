// UDPSocket - High-performance UDP socket implementation

#include "best_server/io/udp_socket.hpp"
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include <cstring>

namespace best_server {
namespace io {

// UDPSocket implementation
UDPSocket::UDPSocket()
    : fd_(-1)
    , event_loop_(nullptr)
    , receiving_(false) {
}

UDPSocket::~UDPSocket() {
    close();
}

bool UDPSocket::bind(const SocketAddress& address) {
    fd_ = socket(AF_INET, SOCK_DGRAM | SOCK_NONBLOCK, 0);
    if (fd_ < 0) {
        return false;
    }
    
    struct sockaddr_in addr;
    std::memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(address.port());
    inet_pton(AF_INET, address.ip().c_str(), &addr.sin_addr);
    
    if (::bind(fd_, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) < 0) {
        ::close(fd_);
        fd_ = -1;
        return false;
    }
    
    local_address_ = address;
    return true;
}

bool UDPSocket::bind(uint16_t port) {
    return bind(SocketAddress("0.0.0.0", port));
}

void UDPSocket::send_to(const SocketAddress& destination, memory::ZeroCopyBuffer&& data, UDPSendCallback callback) {
    if (fd_ < 0) {
        callback(0, std::error_code(EINVAL, std::system_category()));
        return;
    }
    
    struct sockaddr_in addr;
    std::memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(destination.port());
    inet_pton(AF_INET, destination.ip().c_str(), &addr.sin_addr);
    
    ssize_t sent = sendto(fd_, data.data(), data.size(), 0,
                         reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr));
    
    if (sent >= 0) {
        stats_.bytes_sent += sent;
        ++stats_.packets_sent;
        callback(sent, std::error_code());
    } else if (errno == EAGAIN || errno == EWOULDBLOCK) {
        // Would block, queue for later
        send_callbacks_.push_back([this, destination, data = std::move(data), callback = std::move(callback)]() mutable {
            send_to(destination, std::move(data), std::move(callback));
        });
        
        if (event_loop_) {
            event_loop_->modify_fd(fd_, EventType::Read | EventType::Write);
        }
    } else {
        callback(0, std::error_code(errno, std::system_category()));
    }
}

void UDPSocket::send_to_batch(const std::vector<std::pair<SocketAddress, memory::ZeroCopyBuffer>>& messages, UDPSendCallback callback) {
    size_t total_sent = 0;
    
    for (const auto& pair : messages) {
        const auto& destination = pair.first;
        const auto& data = pair.second;
        
        struct sockaddr_in addr;
        std::memset(&addr, 0, sizeof(addr));
        addr.sin_family = AF_INET;
        addr.sin_port = htons(destination.port());
        inet_pton(AF_INET, destination.ip().c_str(), &addr.sin_addr);
        
        ssize_t sent = sendto(fd_, data.data(), data.size(), 0,
                             reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr));
        
        if (sent >= 0) {
            total_sent += sent;
            stats_.bytes_sent += sent;
            ++stats_.packets_sent;
        }
    }
    
    callback(total_sent, std::error_code());
}

void UDPSocket::receive(UDPReceiveCallback callback) {
    if (fd_ < 0) {
        UDPMessage msg;
        callback(std::move(msg), std::error_code(EINVAL, std::system_category()));
        return;
    }
    
    receive_callback_ = std::move(callback);
    receiving_ = true;
    
    if (event_loop_) {
        event_loop_->register_fd(fd_, EventType::Read,
            [this](EventType events) {
                (void)events;
                handle_read_event(events);
            });
    }
}

bool UDPSocket::join_multicast(const std::string& multicast_address) {
    if (fd_ < 0) {
        return false;
    }
    
    struct ip_mreq mreq;
    inet_pton(AF_INET, multicast_address.c_str(), &mreq.imr_multiaddr);
    mreq.imr_interface.s_addr = htonl(INADDR_ANY);
    
    return setsockopt(fd_, IPPROTO_IP, IP_ADD_MEMBERSHIP, &mreq, sizeof(mreq)) == 0;
}

bool UDPSocket::leave_multicast(const std::string& multicast_address) {
    if (fd_ < 0) {
        return false;
    }
    
    struct ip_mreq mreq;
    inet_pton(AF_INET, multicast_address.c_str(), &mreq.imr_multiaddr);
    mreq.imr_interface.s_addr = htonl(INADDR_ANY);
    
    return setsockopt(fd_, IPPROTO_IP, IP_DROP_MEMBERSHIP, &mreq, sizeof(mreq)) == 0;
}

void UDPSocket::set_broadcast(bool enable) {
    if (fd_ < 0) {
        return;
    }
    
    int optval = enable ? 1 : 0;
    setsockopt(fd_, SOL_SOCKET, SO_BROADCAST, &optval, sizeof(optval));
}

void UDPSocket::set_receive_buffer_size(size_t size) {
    if (fd_ < 0) {
        return;
    }
    
    int optval = static_cast<int>(size);
    setsockopt(fd_, SOL_SOCKET, SO_RCVBUF, &optval, sizeof(optval));
}

void UDPSocket::set_send_buffer_size(size_t size) {
    if (fd_ < 0) {
        return;
    }
    
    int optval = static_cast<int>(size);
    setsockopt(fd_, SOL_SOCKET, SO_SNDBUF, &optval, sizeof(optval));
}

void UDPSocket::close() {
    if (fd_ >= 0) {
        if (event_loop_) {
            event_loop_->unregister_fd(fd_);
        }
        ::close(fd_);
        fd_ = -1;
    }
    
    receiving_ = false;
}

SocketAddress UDPSocket::local_address() const {
    return local_address_;
}

void UDPSocket::handle_read_event(EventType events) {
    if (!receiving_ || fd_ < 0) {
        return;
    }
    
    if ((events & EventType::Read) != EventType::None) {
        memory::ZeroCopyBuffer buffer(65536);
        struct sockaddr_in sender_addr;
        socklen_t sender_len = sizeof(sender_addr);
        
        ssize_t received = recvfrom(fd_, buffer.data(), buffer.capacity(), 0,
                                   reinterpret_cast<struct sockaddr*>(&sender_addr), &sender_len);
        
        if (received > 0) {
            stats_.bytes_received += received;
            ++stats_.packets_received;
            
            UDPMessage msg;
            char ip[INET_ADDRSTRLEN];
            inet_ntop(AF_INET, &sender_addr.sin_addr, ip, sizeof(ip));
            msg.sender = SocketAddress(ip, ntohs(sender_addr.sin_port));
            msg.data = std::move(buffer);
            msg.timestamp = 0; // Would need high-resolution timer
            
            if (receive_callback_) {
                receive_callback_(std::move(msg), std::error_code());
            }
        } else if (errno != EAGAIN && errno != EWOULDBLOCK) {
            // Error
            if (receive_callback_) {
                receive_callback_(UDPMessage(), std::error_code(errno, std::system_category()));
            }
        }
    }
}

void UDPSocket::handle_write_event(EventType events) {
    if (fd_ < 0 || send_callbacks_.empty()) {
        return;
    }
    
    if ((events & EventType::Write) != EventType::None) {
        auto callback = std::move(send_callbacks_.front());
        send_callbacks_.erase(send_callbacks_.begin());
        
        callback();
        
        if (send_callbacks_.empty() && event_loop_) {
            event_loop_->modify_fd(fd_, EventType::Read);
        }
    }
}

// UDPServer implementation
UDPServer::UDPServer()
    : socket_(std::make_unique<UDPSocket>())
    , event_loop_(nullptr)
    , running_(false) {
}

UDPServer::~UDPServer() {
    stop();
}

bool UDPServer::start(const SocketAddress& address, MessageHandler handler) {
    if (!socket_->bind(address)) {
        return false;
    }
    
    handler_ = std::move(handler);
    running_ = true;
    
    socket_->set_event_loop(event_loop_);
    socket_->receive([this](UDPMessage&& msg, std::error_code ec) {
        if (!ec && handler_) {
            handler_(std::move(msg), socket_.get());
        }
    });
    
    return true;
}

bool UDPServer::start(uint16_t port, MessageHandler handler) {
    return start(SocketAddress("0.0.0.0", port), std::move(handler));
}

void UDPServer::stop() {
    running_ = false;
    if (socket_) {
        socket_->close();
    }
}

// UDPClientPool implementation
UDPClientPool::UDPClientPool(size_t pool_size)
    : event_loop_(nullptr) {
    
    for (size_t i = 0; i < pool_size; ++i) {
        auto socket = std::make_unique<UDPSocket>();
        socket->bind(0); // Bind to any available port
        sockets_.push_back(std::move(socket));
        available_.push(sockets_.back().get());
    }
}

UDPClientPool::~UDPClientPool() = default;

UDPSocket* UDPClientPool::acquire() {
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (available_.empty()) {
        return nullptr;
    }
    
    auto socket = available_.front();
    available_.pop();
    
    socket->set_event_loop(event_loop_);
    
    return socket;
}

void UDPClientPool::release(UDPSocket* socket) {
    if (!socket) {
        return;
    }
    
    std::lock_guard<std::mutex> lock(mutex_);
    available_.push(socket);
}

bool UDPClientPool::bind_all(const SocketAddress& address) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    for (auto& socket : sockets_) {
        if (!socket->bind(address.port())) {
            return false;
        }
    }
    
    return true;
}

} // namespace io
} // namespace best_server