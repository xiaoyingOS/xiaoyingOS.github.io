// TCPSocket - TCP socket implementation

#include "best_server/io/tcp_socket.hpp"
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include <cstring>
#include <cstdio>

namespace best_server {
namespace io {

// Socket address implementation
SocketAddress::SocketAddress() : ip_("0.0.0.0"), port_(0), valid_(false) {}

SocketAddress::SocketAddress(const std::string& ip, uint16_t port) 
    : ip_(ip), port_(port), valid_(true) {}

std::string SocketAddress::to_string() const {
    return ip_ + ":" + std::to_string(port_);
}

// TCPSocket implementation
TCPSocket::TCPSocket()
    : fd_(-1)
    , connected_(false)
    , closing_(false)
    , remote_address_()
    , local_address_()
    , event_loop_(nullptr)
    , receive_callback_()
    , send_callback_()
    , connect_callback_()
    , stats_()
    , send_queue_()
    , sending_(false)
    , callback_count_(0) {
}

TCPSocket::~TCPSocket() {
    // socket 的生命周期由 HTTPConnection 管理
    // close() 应该在 HTTPConnection::close() 中调用
    // 这里不调用 close() 以避免 double-close 问题
}

void TCPSocket::connect(const SocketAddress& address, ConnectCallback callback) {
    (void)address;
    (void)callback;
    // TODO: Implement async connect
}

void TCPSocket::connect(const SocketAddress& address, ConnectCallback callback, uint32_t timeout_ms) {
    (void)address;
    (void)callback;
    (void)timeout_ms;
    // TODO: Implement async connect with timeout
}

void TCPSocket::send(memory::ZeroCopyBuffer&& buffer, SendCallback callback) {
    (void)buffer;
    (void)callback;
    // TODO: Implement async send
}

void TCPSocket::send(const std::vector<memory::ZeroCopyBuffer>& buffers, SendCallback callback) {
    (void)buffers;
    (void)callback;
    // TODO: Implement async scatter/gather send
}

void TCPSocket::receive(size_t size, ReceiveCallback callback) {
    (void)size;
    (void)callback;
    // TODO: Implement async receive
}

void TCPSocket::receive(memory::ZeroCopyBuffer& buffer, ReceiveCallback callback) {
    (void)buffer;
    (void)callback;
    // TODO: Implement async receive into buffer
}

void TCPSocket::close() {

    printf("DEBUG: TCPSocket::close called, fd=%d, closing=%d\n", fd_, closing_);

    // 获取锁（使用 lock 而不是 try_lock，确保关闭操作完整）
    try {
        close_mutex_.lock();
        printf("DEBUG: Acquired close_mutex for fd=%d\n", fd_);
    } catch (...) {
        printf("DEBUG: Exception in lock, fd=%d\n", fd_);
        return;
    }

    // 在持有锁的情况下检查是否已经在关闭中
    if (closing_ || fd_ < 0) {
        printf("DEBUG: Already closing or closed, fd=%d, closing=%d\n", fd_, closing_);
        close_mutex_.unlock();
        return;
    }

    // 在持有锁的情况下设置关闭标志
    closing_ = true;

    printf("DEBUG: Starting close process for fd=%d\n", fd_);

    // 再次检查 fd（可能在获取锁期间被其他线程关闭）
    if (fd_ < 0) {
        printf("DEBUG: fd became invalid while waiting for lock\n");
        close_mutex_.unlock();
        return;
    }

    // 保存 fd 值并立即将 fd_ 设置为 -1
    int fd_to_close = fd_;
    fd_ = -1;

    printf("DEBUG: fd_to_close=%d, fd_=%d\n", fd_to_close, fd_);
    printf("DEBUG: Unregistering fd=%d from event loop\n", fd_to_close);

    // 安全地注销文件描述符
    if (event_loop_) {
        try {
            event_loop_->unregister_fd(fd_to_close);
            printf("DEBUG: Successfully unregistered fd=%d\n", fd_to_close);
        } catch (...) {
            // 忽略异常，防止崩溃
            printf("DEBUG: Exception in unregister_fd for fd=%d\n", fd_to_close);
        }
    } else {
        printf("DEBUG: event_loop_ is null\n");
    }

    // 释放锁，允许正在执行的回调完成
    printf("DEBUG: Releasing lock to wait for callbacks to complete, callback_count=%d\n", 
           callback_count_.load());
    close_mutex_.unlock();

    // 等待其他回调完成（允许引用计数为1，表示当前正在 close() 中执行的回调）
    int timeout_ms = 5000;  // 最多等待5秒
    int elapsed_ms = 0;
    while (callback_count_.load() > 1 && elapsed_ms < timeout_ms) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        elapsed_ms += 10;
    }
    
    if (callback_count_.load() > 1) {
        printf("DEBUG: Warning: Still have %d active callbacks after timeout\n", 
               callback_count_.load());
    } else {
        printf("DEBUG: All other callbacks completed, current callback will finish\n");
    }

    // 重新获取锁
    close_mutex_.lock();
    printf("DEBUG: Re-acquired lock after waiting for callbacks\n");

    printf("DEBUG: Closing fd=%d\n", fd_to_close);

    // 安全地关闭文件描述符
    if (fd_to_close >= 0) {
        printf("DEBUG: About to call close() on fd=%d\n", fd_to_close);
        int result = ::close(fd_to_close);
        printf("DEBUG: close() returned %d for fd=%d\n", result, fd_to_close);
        if (result < 0) {
            printf("DEBUG: close() failed for fd=%d, errno=%d (%s)\n", 
                   fd_to_close, errno, strerror(errno));
        } else {
            printf("DEBUG: Successfully closed fd=%d\n", fd_to_close);
        }
    }

    connected_ = false;
    sending_ = false;
    
    printf("DEBUG: Clearing send queue for fd=%d\n", fd_to_close);
    while (!send_queue_.empty()) {
        send_queue_.pop();
    }

    // 释放锁
    close_mutex_.unlock();
    
    // 简单的同步点，确保锁的释放完成
    std::atomic_thread_fence(std::memory_order_acquire);
}

SocketAddress TCPSocket::local_address() const {
    // 获取锁以防止在 close() 执行期间访问失效的 fd
    close_mutex_.lock();
    
    int fd_copy = fd_;
    close_mutex_.unlock();
    
    if (fd_copy < 0) {
        return SocketAddress();
    }
    
    struct sockaddr_in addr;
    socklen_t len = sizeof(addr);
    if (getsockname(fd_copy, reinterpret_cast<struct sockaddr*>(&addr), &len) == 0) {
        char ip[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &addr.sin_addr, ip, sizeof(ip));
        return SocketAddress(ip, ntohs(addr.sin_port));
    }
    
    return SocketAddress();
}

SocketAddress TCPSocket::remote_address() const {
    return remote_address_;
}

void TCPSocket::set_keep_alive(bool enable) {
    close_mutex_.lock();
    int fd_copy = fd_;
    close_mutex_.unlock();
    
    if (fd_copy >= 0) {
        int optval = enable ? 1 : 0;
        setsockopt(fd_copy, SOL_SOCKET, SO_KEEPALIVE, &optval, sizeof(optval));
    }
}

void TCPSocket::set_tcp_no_delay(bool enable) {
    close_mutex_.lock();
    int fd_copy = fd_;
    close_mutex_.unlock();
    
    if (fd_copy >= 0) {
        int optval = enable ? 1 : 0;
        setsockopt(fd_copy, IPPROTO_TCP, TCP_NODELAY, &optval, sizeof(optval));
    }
}

void TCPSocket::handle_connect_event(EventType events) {
    (void)events;
    // TODO: Implement connect event handling
}

void TCPSocket::handle_read_event(EventType events) {
    // DEBUG: log non-read events (error events)
    if ((events & EventType::Read) == EventType::None) {
        // printf("DEBUG: handle_read_event: ignoring non-read event, events=%d\n", static_cast<int>(events));
        return;
    }
    
    handle_incoming_data();
}

void TCPSocket::handle_write_event(EventType events) {
    // 增加引用计数
    callback_count_++;
    
    // Hold the lock for the entire method to prevent concurrent close
    std::lock_guard<std::mutex> lock(close_mutex_);

    // Only process write events (EPOLLOUT)
    if ((events & EventType::Write) == EventType::None) {
        // Don't log this - it causes infinite loops when epoll triggers error events
        // printf("DEBUG: handle_write_event: ignoring non-write event, events=%d\n", static_cast<int>(events));
        callback_count_--;
        return;
    }

    // Check if socket is still valid before processing
    if (fd_ < 0) {
        printf("DEBUG: handle_write_event: fd is invalid, returning\n");
        callback_count_--;
        return;
    }

    // 在level-triggered模式下，如果不需要发送数据，直接返回而不修改fd
    // 这样可以避免在socket仍然可写时的死循环
    if (!sending_ || send_queue_.empty()) {
        // DEBUG: log when stopping write events
        // printf("DEBUG: handle_write_event: sending_=%d, send_queue_.empty()=%d\n", sending_, send_queue_.empty());
        sending_ = false;
        // 不要修改fd，让epoll继续触发写事件，但我们会直接返回
        // 这样可以避免level-triggered模式下的死循环
        return;
    }

    // DEBUG: log send queue size
    // printf("DEBUG: handle_write_event: send_queue_.size()=%zu\n", send_queue_.size());

    // In level-triggered mode, continue sending until queue is empty or EAGAIN
    while (!send_queue_.empty()) {
        // Check if socket is still valid before each send operation
        if (fd_ < 0) {
            printf("DEBUG: handle_write_event: socket closed during send loop, breaking\n");
            break;
        }

        auto buffer = send_queue_.front();

        // DEBUG: log send size
        // printf("DEBUG: handle_write_event: sending %zu bytes\n", buffer.size());

        ssize_t bytes_sent = ::send(fd_, buffer.data(), buffer.size(), MSG_NOSIGNAL);

        // DEBUG: log sent bytes
        // printf("DEBUG: handle_write_event: sent %zd bytes\n", bytes_sent);

        if (bytes_sent > 0) {
            stats_.bytes_sent += bytes_sent;
            ++stats_.packets_sent;

            if (static_cast<size_t>(bytes_sent) == buffer.size()) {
                send_queue_.pop();
            } else {
                buffer.consume(bytes_sent);
                // 部分发送，继续处理剩余数据，不弹出队列
                break;
            }

            if (send_callback_) {
                send_callback_(bytes_sent, std::error_code());
                send_callback_ = nullptr;
            }
        } else if (bytes_sent < 0) {
            printf("DEBUG: handle_write_event: send error, errno=%d (%s)\n", errno, strerror(errno));

            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                // Socket buffer is full, wait for next write event
                return;
            } else {
                // Error
                std::error_code ec(errno, std::system_category());
                if (send_callback_) {
                    send_callback_(0, ec);
                    send_callback_ = nullptr;
                }
                sending_ = false;
                if (event_loop_ && fd_ >= 0) {
                    event_loop_->modify_fd(fd_, EventType::Read);
                }
                return;
            }
        } else {
            // bytes_sent == 0, shouldn't happen
            printf("DEBUG: handle_write_event: bytes_sent == 0, shouldn't happen\n");
            break;
        }
    }

    // 检查队列是否为空，如果为空则停止发送
    if (send_queue_.empty()) {
        sending_ = false;
        if (event_loop_ && fd_ >= 0) {
            event_loop_->modify_fd(fd_, EventType::Read);
        }
    }
    
    // 减少引用计数
    callback_count_--;
}

void TCPSocket::handle_incoming_data() {
    // 增加引用计数
    callback_count_++;
    
    // DEBUG: log when handle_incoming_data is called
    // printf("DEBUG: handle_incoming_data called, fd=%d\n", fd_);

    if (fd_ < 0) {
        callback_count_--;
        return;
    }

    

    // Read available data - 使用动态分配的缓冲区支持大文件上传

    // 原来 8KB 太小，改为 64KB 提高效率

    size_t buffer_size = 65536;  // 64KB

    std::vector<char> buffer(buffer_size);

    ssize_t bytes_read = ::read(fd_, buffer.data(), buffer_size);

    

    // DEBUG: log bytes read (commented out to reduce log volume)
    // printf("DEBUG: Read %zd bytes from fd=%d\n", bytes_read, fd_);

    

    if (bytes_read > 0) {

        stats_.bytes_received += bytes_read;

        ++stats_.packets_received;

        

        

        if (receive_callback_) {

            // DEBUG: log callback invocation
            // printf("DEBUG: Calling receive_callback with %zd bytes\n", bytes_read);



            if (receive_callback_) {

                receive_callback_(memory::ZeroCopyBuffer(buffer.data(), bytes_read), std::error_code());

                receive_callback_ = nullptr;

            }

        } else {

            printf("DEBUG: No receive_callback set!\n");

        }
        
        callback_count_--;

    } else if (bytes_read == 0) {

        // Connection closed - IMPORTANT: always log this

        printf("DEBUG: Connection closed on fd=%d\n", fd_);

        close();

        if (receive_callback_) {

            receive_callback_(memory::ZeroCopyBuffer(), std::error_code(0, std::system_category()));

            receive_callback_ = nullptr;

        }
        
        callback_count_--;

    } else if (errno != EAGAIN && errno != EWOULDBLOCK) {

        // Error - IMPORTANT: always log this

        std::error_code ec(errno, std::system_category());

        printf("DEBUG: Read error on fd=%d: %s\n", fd_, ec.message().c_str());

        if (receive_callback_) {

            receive_callback_(memory::ZeroCopyBuffer(), ec);

            receive_callback_ = nullptr;

        }
        
        callback_count_--;

    } else {

        // EAGAIN - normal, no need to log

        // printf("DEBUG: EAGAIN on fd=%d, no data available\n", fd_);
        
        callback_count_--;

    }

}

future::Future<memory::ZeroCopyBuffer> TCPSocket::read_async(size_t size) {
    (void)size;

    auto promise = std::make_shared<future::Promise<memory::ZeroCopyBuffer>>();
    auto future = promise->get_future();

    receive_callback_ = [promise](memory::ZeroCopyBuffer&& data, std::error_code ec) {
        if (!ec) {
            try {
                promise->set_value(std::move(data));
            } catch (const std::exception& e) {
            } catch (...) {
            }
        } else {
            try {
                promise->set_exception(std::make_exception_ptr(std::system_error(ec)));
            } catch (const std::exception& e) {
            } catch (...) {
            }
        }
    };
    
    if (event_loop_) {
        // Register fd with read event callback
        // The callback will call handle_read_event when data is available
        event_loop_->register_fd(fd_, EventType::Read,
            [this](EventType events) {
                handle_read_event(events);
            });
    } else {
    }
    
    return future;
}

future::Future<size_t> TCPSocket::write_async(const std::vector<uint8_t>& data) {
    auto promise = std::make_shared<future::Promise<size_t>>();
    auto future = promise->get_future();

    send_queue_.push(memory::ZeroCopyBuffer(data.size()));
    std::memcpy(send_queue_.back().data(), data.data(), data.size());

    send_callback_ = [promise](size_t sent, std::error_code ec) {
        if (!ec) {
            promise->set_value(sent);
        } else {
            promise->set_exception(std::make_exception_ptr(std::system_error(ec)));
        }
    };

    sending_ = true;
    if (event_loop_) {
        // Register or modify fd with write event
        event_loop_->register_fd(fd_, EventType::Write,
            [this](EventType events) {
                handle_write_event(events);
            });
        // Don't call handle_write_event immediately - let epoll trigger it
        // This ensures then() callbacks can be registered before set_value() is called
    }

    return future;
}

future::Future<size_t> TCPSocket::write_async(const memory::ZeroCopyBuffer& data) {
    auto promise = std::make_shared<future::Promise<size_t>>();
    auto future = promise->get_future();

    send_queue_.push(data);

    send_callback_ = [promise](size_t sent, std::error_code ec) {
        if (!ec) {
            promise->set_value(sent);
        } else {
            promise->set_exception(std::make_exception_ptr(std::system_error(ec)));
        }
    };

    sending_ = true;
    if (event_loop_) {
        // Register or modify fd with write event
        event_loop_->register_fd(fd_, EventType::Write,
            [this](EventType events) {
                handle_write_event(events);
            });
        // Don't call handle_write_event immediately - let epoll trigger it
        // This ensures then() callbacks can be registered before set_value() is called
    }

    return future;
}

void TCPSocket::set_send_buffer_size(size_t size) {
    if (fd_ >= 0) {
        setsockopt(fd_, SOL_SOCKET, SO_SNDBUF, &size, sizeof(size));
    }
}

void TCPSocket::set_receive_buffer_size(size_t size) {
    if (fd_ >= 0) {
        setsockopt(fd_, SOL_SOCKET, SO_RCVBUF, &size, sizeof(size));
    }
}

void TCPSocket::enable_fast_open(bool enable) {
    (void)enable;
    // TODO: Implement TCP Fast Open
}

future::Future<void> TCPSocket::wait_readable() {
    auto promise = std::make_shared<future::Promise<void>>();
    auto future = promise->get_future();
    
    receive_callback_ = [promise](memory::ZeroCopyBuffer&&, std::error_code ec) {
        if (!ec) {
            promise->set_value();
        } else {
            promise->set_exception(std::make_exception_ptr(std::system_error(ec)));
        }
    };
    
    if (event_loop_) {
        event_loop_->modify_fd(fd_, EventType::Read);
    }
    
    return future;
}

future::Future<void> TCPSocket::wait_writable() {
    auto promise = std::make_shared<future::Promise<void>>();
    auto future = promise->get_future();
    
    send_callback_ = [promise](size_t, std::error_code ec) {
        if (!ec) {
            promise->set_value();
        } else {
            promise->set_exception(std::make_exception_ptr(std::system_error(ec)));
        }
    };
    
    sending_ = true;
    if (event_loop_) {
        event_loop_->modify_fd(fd_, EventType::Write);
    }
    
    return future;
}

bool TCPSocket::connect_sync(const std::string& host, uint16_t port, uint32_t timeout_ms) {
    (void)host;
    (void)port;
    (void)timeout_ms;
    // TODO: Implement sync connect
    return false;
}

// TCPAcceptor implementation
TCPAcceptor::TCPAcceptor()
    : fd_(-1)
    , address_()
    , event_loop_(nullptr)
    , accept_callback_() {
}

TCPAcceptor::~TCPAcceptor() {
    stop();
}

bool TCPAcceptor::bind(const SocketAddress& address, int backlog) {

    std::string ip = address.ip();
    bool is_ipv6 = (ip.find(":") != std::string::npos);
    
    // 如果是 0.0.0.0，使用 IPv6 双栈模式（支持 IPv4 和 IPv6）
    bool use_dual_stack = (ip == "0.0.0.0");
    
    int family = use_dual_stack ? AF_INET6 : (is_ipv6 ? AF_INET6 : AF_INET);
    
    // Create socket
    fd_ = socket(family, SOCK_STREAM | SOCK_NONBLOCK, 0);
    if (fd_ < 0) {
        return false;
    }

    // Set SO_REUSEADDR
    int optval = 1;
    if (setsockopt(fd_, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval)) < 0) {
        close(fd_);
        fd_ = -1;
        return false;
    }
    
    // If using IPv6, disable IPV6_V6ONLY to support IPv4 connections
    if (family == AF_INET6) {
        int v6only = 0;
        if (setsockopt(fd_, IPPROTO_IPV6, IPV6_V6ONLY, &v6only, sizeof(v6only)) < 0) {
            // If setting IPV6_V6ONLY fails, continue anyway
            // Some systems may not support this option
        }
    }

    // Bind to address
    if (family == AF_INET6) {
        struct sockaddr_in6 addr6;
        memset(&addr6, 0, sizeof(addr6));
        addr6.sin6_family = AF_INET6;
        addr6.sin6_port = htons(address.port());
        
        if (use_dual_stack) {
            // Bind to all IPv6 addresses (which also accepts IPv4)
            addr6.sin6_addr = in6addr_any;
        } else {
            // Bind to specific IPv6 address
            inet_pton(AF_INET6, ip.c_str(), &addr6.sin6_addr);
        }
        
        if (::bind(fd_, reinterpret_cast<struct sockaddr*>(&addr6), sizeof(addr6)) < 0) {
            close(fd_);
            fd_ = -1;
            return false;
        }
    } else {
        struct sockaddr_in addr;
        memset(&addr, 0, sizeof(addr));
        addr.sin_family = AF_INET;
        addr.sin_port = htons(address.port());
        inet_pton(AF_INET, ip.c_str(), &addr.sin_addr);

        if (::bind(fd_, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) < 0) {
            close(fd_);
            fd_ = -1;
            return false;
        }
    }

    // Listen
    if (::listen(fd_, backlog) < 0) {
        close(fd_);
        fd_ = -1;
        return false;
    }

    address_ = address;
    return true;
}

void TCPAcceptor::accept(AcceptCallback callback) {

    accept_callback_ = std::move(callback);

    if (event_loop_ && fd_ >= 0) {
        event_loop_->register_fd(fd_, EventType::Accept,
            [this](EventType events) {
                handle_accept_event(events);
            });
    } else {
    }
}

void TCPAcceptor::stop() {

    if (fd_ >= 0) {
        if (event_loop_) {
            event_loop_->unregister_fd(fd_);
        }
        close(fd_);
        fd_ = -1;
    }

    accept_callback_ = nullptr;
}

SocketAddress TCPAcceptor::address() const {
    return address_;
}

bool TCPAcceptor::connect_sync(const std::string& host, uint16_t port, uint32_t timeout_ms) {
    (void)host;
    (void)port;
    (void)timeout_ms;
    return false;
}

void TCPAcceptor::handle_accept_event(EventType events) {

    // DEBUG: log accept event (commented out to reduce log volume)
    // printf("DEBUG: handle_accept_event called, events=%d\n", static_cast<int>(events));

    (void)events;



    int connection_count = 0;  // DEBUG



    while (true) {



    (void)connection_count;  // Suppress unused warning



        struct sockaddr_in client_addr;



        socklen_t client_len = sizeof(client_addr);



        int client_fd = ::accept4(fd_, reinterpret_cast<struct sockaddr*>(&client_addr), &client_len, SOCK_NONBLOCK);



        if (client_fd < 0) {



            if (errno == EAGAIN || errno == EWOULDBLOCK) {

                // Normal: no more pending connections
                // printf("DEBUG: accept4 returned EAGAIN, no more pending connections\n");

                // No more pending connections

                break;

            } else {

                // Error - IMPORTANT: always log this

                printf("DEBUG: accept4 error: %s\n", strerror(errno));

                break;

            }

        }



        // IMPORTANT: log new connections
        printf("DEBUG: Accepted new connection, fd=%d\n", client_fd);

        (void)connection_count++;  // Use variable to avoid warning

        // Get client address
        char client_ip[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &client_addr.sin_addr, client_ip, INET_ADDRSTRLEN);
        uint16_t client_port = ntohs(client_addr.sin_port);


        // Create TCPSocket
        auto socket = std::make_unique<TCPSocket>();
        socket->fd_ = client_fd;
        socket->connected_ = true;
        socket->remote_address_ = SocketAddress(client_ip, client_port);
        socket->event_loop_ = event_loop_;

        // Call accept callback
        if (accept_callback_) {
            accept_callback_(std::move(socket), std::error_code());
        } else {
            close(client_fd);
        }
    }
}

// TCP Connection Pool
TCPConnectionPool::TCPConnectionPool(size_t max_connections)
    : event_loop_(nullptr)
    , max_connections_(max_connections) {
    (void)max_connections_;  // Suppress unused warning
}

TCPConnectionPool::~TCPConnectionPool() = default;

void TCPConnectionPool::get_connection(const SocketAddress& address, ConnectionCallback callback) {
    (void)address;
    (void)callback;
    // TODO: Implement connection pooling
}

void TCPConnectionPool::return_connection(std::shared_ptr<TCPSocket> socket) {
    (void)socket;
    // TODO: Implement connection return
}

size_t TCPConnectionPool::active_connections() const {
    return 0;
}

size_t TCPConnectionPool::idle_connections() const {
    return 0;
}

} // namespace io
} // namespace best_server