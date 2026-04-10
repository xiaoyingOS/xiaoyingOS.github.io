// HTTPServer implementation
#include "best_server/network/http_server.hpp"
#include "best_server/network/http_parser.hpp"
#include "best_server/network/http2_server.hpp"
#include <algorithm>
#include <regex>
#include <cstring>
#include <sys/socket.h>
#include <sys/stat.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <unistd.h>
#include <fcntl.h>
#include <filesystem>
#include <thread>

namespace best_server {
namespace network {

// HTTPConnection implementation
HTTPConnection::HTTPConnection(std::shared_ptr<io::TCPSocket> socket)
    : socket_(socket)
    , keep_alive_(true)
    , request_count_(0)
    , last_activity_time_(std::chrono::steady_clock::now().time_since_epoch().count())
{
    // Enable TCP_NODELAY to disable Nagle's algorithm for better latency
    if (socket_) {
        socket_->set_tcp_no_delay(true);
    }
}

HTTPConnection::~HTTPConnection() {
    close();
}

void HTTPConnection::update_activity_time() {
    last_activity_time_ = std::chrono::steady_clock::now().time_since_epoch().count();
}

void HTTPConnection::close() {
    std::lock_guard<std::mutex> lock(close_mutex_);
    
    if (!socket_ || closed_) {
        return;
    }
    
    closed_ = true;
    
    // 先清理 read_future_，防止新的回调被调度
    read_future_ = future::Future<void>();
    
    // 调用 close()，它会等待所有回调完成
    socket_->close();
    
    // 等待一小段时间，确保所有异步回调都完全执行完毕
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    
    // 移除 socket 引用，使其可以被销毁
    socket_.reset();
    
    printf("DEBUG: HTTPConnection::close completed\n");
}
void HTTPConnection::reset() {
    keep_alive_ = true;
    request_count_ = 0;
    last_activity_time_ = std::chrono::steady_clock::now().time_since_epoch().count();
    reset_file_stream_state();
    
    // 清理 read_future_，防止重用连接时旧的异步回调还在执行
    read_future_ = future::Future<void>();
}

// HTTPConnectionPool implementation
HTTPConnectionPool::HTTPConnectionPool() : max_pool_size_(MAX_POOL_SIZE) {
    // Initialize shards
}

HTTPConnectionPool::~HTTPConnectionPool() {
    // Clean up all shards
    for (auto& shard : shards_) {
        std::lock_guard<std::mutex> lock(shard.mutex);
        shard.idle_connections.clear();
        shard.active_connections.clear();
    }
}

std::shared_ptr<HTTPConnection> HTTPConnectionPool::acquire(std::unique_ptr<io::TCPSocket> socket) {
    // Distribute across shards using socket pointer hash
    size_t shard_idx = std::hash<void*>{}(socket.get()) % NUM_SHARDS;
    auto& shard = shards_[shard_idx];
    
    std::lock_guard<std::mutex> lock(shard.mutex);
    
    // Try to reuse idle connection
    if (!shard.idle_connections.empty()) {
        auto conn = std::move(shard.idle_connections.back());
        shard.idle_connections.pop_back();
        
        conn->reset();
        // Note: socket replacement not supported in this design
        // Just return the idle connection without updating socket
        return conn;
    }
    
    // Create new connection
    auto conn = std::make_shared<HTTPConnection>(std::move(socket));
    shard.active_connections[conn.get()] = shard.total_connections++;
    return conn;
}

void HTTPConnectionPool::release(std::shared_ptr<HTTPConnection> conn) {
    if (!conn) return;
    
    size_t shard_idx = get_shard(conn.get());
    auto& shard = shards_[shard_idx];
    
    std::lock_guard<std::mutex> lock(shard.mutex);
    
    // Remove from active
    shard.active_connections.erase(conn.get());
    
    // Check if connection can be reused (keep-alive and not too many requests)
    if (conn->is_keep_alive() && conn->request_count() < 1000 && 
        shard.idle_connections.size() < max_pool_size_) {
        // 重置连接状态，但不关闭 socket
        conn->reset();
        shard.idle_connections.push_back(std::move(conn));
    }
    // Otherwise, let it be destroyed
}

HTTPConnectionPool::PoolStats HTTPConnectionPool::stats() const {
    PoolStats total_stats;
    
    // Aggregate stats from all shards
    for (const auto& shard : shards_) {
        std::lock_guard<std::mutex> lock(shard.mutex);
        total_stats.total_connections += shard.total_connections;
        total_stats.idle_connections += shard.idle_connections.size();
        total_stats.active_connections += shard.active_connections.size();
    }
    
    return total_stats;
}

// Get shard index for a connection
size_t HTTPConnectionPool::get_shard(HTTPConnection* conn) const {
    return std::hash<HTTPConnection*>{}(conn) % NUM_SHARDS;
}

// Get shard index by connection ID
size_t HTTPConnectionPool::get_shard_by_id(size_t conn_id) const {
    return conn_id % NUM_SHARDS;
}

void HTTPConnectionPool::cleanup_idle_connections(uint64_t timeout_ms) {
    uint64_t now = std::chrono::steady_clock::now().time_since_epoch().count();
    
    // Clean up all shards
    for (auto& shard : shards_) {
        std::lock_guard<std::mutex> lock(shard.mutex);
        
        auto it = shard.idle_connections.begin();
        while (it != shard.idle_connections.end()) {
            if (now - (*it)->last_activity_time() > timeout_ms * 1000000ULL) {
                it = shard.idle_connections.erase(it);
            } else {
                ++it;
            }
        }
    }
}

// HTTPServer implementation
HTTPServer::HTTPServer()
    : https_enabled_(false)
{
    not_found_handler_ = [](HTTPRequest& /*req*/, HTTPResponse& res) {
        res.set_status(HTTPStatus::NotFound);
        res.set_html("<html><body><h1>404 Not Found</h1><p>The requested resource was not found on this server.</p></body></html>");
    };
    
    error_handler_ = [](HTTPRequest& /*req*/, HTTPResponse& res) {
        res.set_status(HTTPStatus::InternalServerError);
        res.set_html("<html><body><h1>500 Internal Server Error</h1><p>An error occurred while processing your request.</p></body></html>");
    };
}

HTTPServer::~HTTPServer() {
    stop();
}

bool HTTPServer::start(const HTTPServerConfig& config) {
    if (running_.load()) {
        return false;
    }
    
    config_ = config;
    
    // Start worker threads
    workers_running_.store(true);
    size_t num_workers = std::max(4u, std::thread::hardware_concurrency());
    for (size_t i = 0; i < num_workers; ++i) {
        worker_threads_.emplace_back([this]() {
            while (workers_running_.load()) {
                std::function<void()> task;
                {
                    std::unique_lock<std::mutex> lock(task_mutex_);
                    task_cv_.wait(lock, [this]() {
                        return !task_queue_.empty() || !workers_running_.load();
                    });
                    
                    if (!workers_running_.load()) {
                        break;
                    }
                    
                    if (!task_queue_.empty()) {
                        task = std::move(task_queue_.front());
                        task_queue_.pop();
                    }
                }
                
                if (task) {
                    task();
                }
            }
        });
    }
    
    // Create event loop for handling I/O events
    event_loop_ = std::make_unique<io::IOEventLoop>();
    
    // Create TCP acceptor
    acceptor_ = std::make_unique<io::TCPAcceptor>();
    acceptor_->set_event_loop(event_loop_.get());
    
    // Create SocketAddress from config
    io::SocketAddress address(config_.address, config_.port);
    
    if (!acceptor_->bind(address, config_.backlog)) {
        // printf("Failed to bind to %s:%d\n", config_.address.c_str(), config_.port);
        workers_running_.store(false);
        task_cv_.notify_all();
        for (auto& thread : worker_threads_) {
            if (thread.joinable()) {
                thread.join();
            }
        }
        worker_threads_.clear();
        return false;
    }
    
    // Register accept callback (this will be called by event loop when new connections arrive)
    // Note: In edge-triggered mode, the callback will handle all pending connections
    acceptor_->accept([this](std::shared_ptr<io::TCPSocket> socket, std::error_code ec) {
        if (!running_.load()) {
            return;
        }
        
        if (ec || !socket) {
            // printf("Accept failed: %s\n", ec.message().c_str());
            return;
        }
        
        handle_accepted_socket(socket);
    });
    
    running_.store(true);
    
    
    // Start event loop in a background thread
    std::thread event_loop_thread([this]() {
        if (event_loop_) {
            event_loop_->run();
        }
    });
    event_loop_thread.detach();
    
    // Start connection cleanup thread
    std::thread cleanup_thread([this]() {
        while (running_.load()) {
            std::this_thread::sleep_for(std::chrono::seconds(10));
            this->cleanup_idle_connections();
        }
    });
    cleanup_thread.detach();
    
    
    return true;
}

void HTTPServer::stop() {
    if (!running_.load()) {
        return;
    }
    
    running_.store(false);
    
    // Stop acceptor first (this will stop accepting new connections)
    if (acceptor_) {
        acceptor_->stop();
        acceptor_.reset();
    }
    
    // Close all connections
    {
        std::lock_guard<std::mutex> lock(connections_mutex_);
        for (auto& [addr, conn] : connections_) {
            conn->close();
        }
        connections_.clear();
    }
    
    // Stop event loop
    if (event_loop_) {
        event_loop_->stop();
        event_loop_.reset();
    }
    
    // Stop worker threads
    workers_running_.store(false);
    task_cv_.notify_all();
    for (auto& thread : worker_threads_) {
        if (thread.joinable()) {
            thread.join();
        }
    }
    worker_threads_.clear();
}

void HTTPServer::accept_loop() {
    // Use async accept with callback
    acceptor_->accept([this](std::shared_ptr<io::TCPSocket> socket, std::error_code ec) {
        if (!running_.load()) {
            return;
        }
        
        if (ec || !socket) {
            // Accept failed, try again
            if (running_.load()) {
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
                // Continue accepting
                accept_loop();
            }
            return;
        }
        
        handle_accepted_socket(socket);
        
        // Continue accepting
        if (running_.load()) {
            accept_loop();
        }
    });
}

void HTTPServer::handle_accepted_socket(std::shared_ptr<io::TCPSocket> socket) {
    
    // Check connection limit
    if (connections_.size() >= config_.max_connections) {
        socket->close();
        return;
    }
    
    // Set event loop for the socket
    socket->set_event_loop(event_loop_.get());
    
    // Create connection first
    auto conn = std::make_shared<HTTPConnection>(std::move(socket));
    std::string remote_addr = conn->socket()->remote_address().to_string();
    
    
    {
        std::lock_guard<std::mutex> lock(connections_mutex_);
        connections_[remote_addr] = conn;
    }
    
    
    // Start async connection handling (save the future to prevent it from being destroyed)
    auto connection_future = handle_connection(remote_addr);
    
    
    stats_.total_connections++;
    stats_.active_connections++;
}

future::Future<void> HTTPServer::handle_connection(const std::string& remote_addr) {
    
    std::shared_ptr<HTTPConnection> conn;
    {
        std::lock_guard<std::mutex> lock(connections_mutex_);
        auto it = connections_.find(remote_addr);
        if (it == connections_.end()) {
            return future::make_ready_future();
        }
        conn = it->second;
    }
    
    conn->update_activity_time();
    
    
    // Create parser on heap (since HTTPParser is not copyable)
    auto parser = std::make_unique<HTTPParser>();
    HTTPParser* parser_ptr = parser.get();
    
    
    parser->set_events({
        .on_request_line = [](HTTPMethod, const std::string&, HTTPVersion) {},
        .on_header = [](const std::string&, const std::string&) {},
        .on_body = [](const char*, size_t) {},
        .on_complete = [this, conn]() {
            stats_.total_requests++;
            conn->increment_request_count();
        },
        .on_error = [](const std::string& /*error*/) {
            // Log error
        }
    });
    
    
    // Keep parser alive during async operations
    auto parser_holder = std::shared_ptr<HTTPParser>(parser.release());
    
    
    
    // Read and process requests asynchronously
    // Save the future to prevent it from being destroyed
    auto future = process_requests_async(conn, parser_ptr, remote_addr, parser_holder);
    conn->set_read_future(std::move(future));
    
    return future::make_ready_future();
}

future::Future<void> HTTPServer::process_requests_async(
    std::shared_ptr<HTTPConnection> conn,
    HTTPParser* parser,
    const std::string& remote_addr,
    std::shared_ptr<HTTPParser> parser_holder) {
    
    try {
        
        printf("DEBUG: process_requests_async called, keep_alive=%d, request_count=%d, max_requests=%d, running=%d\n",
               conn->is_keep_alive(), conn->request_count(), config_.max_requests_per_connection, running_.load());
        fflush(stdout);

        if (!conn->is_keep_alive() ||
            conn->request_count() >= config_.max_requests_per_connection ||
            !running_.load()) {
            printf("DEBUG: process_requests_async closing connection\n");
            // fflush(stdout);
            return close_connection_async(remote_addr);
        }
        
        // fflush(stderr);
        
        // Asynchronously read data - 使用更大的缓冲区以提高大文件上传性能
        size_t read_size = 65536;  // 64KB 缓冲区，原来是 8192
        auto read_future = conn->socket()->read_async(read_size);
        
        // fflush(stderr);
        
        return read_future.then(
            [this, conn, parser, remote_addr, parser_holder](memory::ZeroCopyBuffer buffer) {
                
                // Check if parser pointer is valid
                if (parser == nullptr) {
                    return;
                }
                
                // Check if conn is still valid
                if (!conn) {
                    return;
                }
                
                // Check if connection is closed
                if (conn->is_closed()) {
                    return;
                }
                
                // Check if server is still running
                if (!running_.load()) {
                    return;
                }
                
                if (buffer.size() == 0) {
                    close_connection_async(remote_addr);
                    return;
                }
                
                // Check if socket is still valid (connection not closed)
                if (!conn->socket()) {
                    return;
                }
                
                // Parse request
                parser->parse_request(buffer.data(), buffer.size());
                
                if (!parser->is_complete()) {
                    // Need more data, continue reading
                    process_requests_async(conn, parser, remote_addr, parser_holder);
                    return;
                }
                
                // Handle the complete request
                handle_request_async(conn, parser, remote_addr, parser_holder).then(
                    [this, conn, parser, remote_addr, parser_holder]() {
                        try {
                            printf("DEBUG: handle_request_async then callback called\n");
                            // fflush(stdout);
                            
                            // Check if connection is closed before resetting parser
                            if (!conn || conn->is_closed()) {
                                return;
                            }
                            
                            // Reset parser and process next request
                            parser->reset();
                            
                            printf("DEBUG: parser reset, calling process_requests_async again\n");
                            // fflush(stdout);
                            
                            // Check if connection is still valid before processing next request
                            if (conn && conn->socket() && running_.load() && !conn->is_closed()) {
                                // Save the future to prevent it from being destroyed
                                auto future = process_requests_async(conn, parser, remote_addr, parser_holder);
                                conn->set_read_future(std::move(future));
                            }
                            
                            printf("DEBUG: process_requests_async called\n");
                            // fflush(stdout);
                        } catch (const std::exception& e) {
                            printf("DEBUG: Exception in then callback: %s\n", e.what());
                            // fflush(stdout);
                        } catch (...) {
                            printf("DEBUG: Unknown exception in then callback\n");
                            // fflush(stdout);
                        }
                    });
            });
    } catch (const std::exception& e) {
        return future::make_ready_future();
    } catch (...) {
        return future::make_ready_future();
    }
}

future::Future<void> HTTPServer::handle_request_async(
    std::shared_ptr<HTTPConnection> conn,
    HTTPParser* parser,
    const std::string& remote_addr,
    std::shared_ptr<HTTPParser> parser_holder) {
    
    (void)parser_holder;  // Suppress unused warning
    
    HTTPRequest* request = parser->request();
    HTTPResponse response;
    
    request->set_remote_address(remote_addr);
    
    // Check for HTTP/2 upgrade request
    if (config_.enable_http2 && request->method() == HTTPMethod::GET) {
        std::string upgrade_header = request->get_header("Upgrade", "");
        if (upgrade_header == "h2c") {
            // Handle HTTP/2 upgrade
            
            response.set_status(HTTPStatus::SwitchingProtocols);
            response.set_header("Connection", "Upgrade");
            response.set_header("Upgrade", "h2c");
            
            // Send upgrade response
            auto response_buffer = response.serialize();
            return conn->socket()->write_async(response_buffer).then(
                [conn, remote_addr](size_t bytes_sent) {
                    (void)bytes_sent;
                    
                    // Send HTTP/2 settings frame on the existing connection
                    network::HTTP2Frame settings_frame;
                    settings_frame.type = network::HTTP2FrameType::SETTINGS;
                    settings_frame.flags = 0x0;
                    settings_frame.stream_id = 0;
                    
                    // Build settings frame
                    uint8_t header[9];
                    header[0] = 0;
                    header[1] = 0;
                    header[2] = 0;
                    header[3] = static_cast<uint8_t>(network::HTTP2FrameType::SETTINGS);
                    header[4] = 0x0;
                    header[5] = 0;
                    header[6] = 0;
                    header[7] = 0;
                    header[8] = 0;
                    
                    memory::ZeroCopyBuffer buffer(9);
                    buffer.write(header, 9);
                    
                    std::vector<uint8_t> data(buffer.data(), buffer.data() + buffer.size());
                    conn->socket()->write_async(data).then([](size_t bytes_sent) {
                        (void)bytes_sent;
                    });
                });
        }
    }
    
    // Apply middleware and handle request
    apply_middleware(*request, response, 0);
    
    // Check if this is a streaming file response
    if (response.is_streaming_file()) {
        // 流式文件传输
        const std::string& file_path = response.stream_file_path();
        
        // 打开文件（非阻塞模式）
        int fd = ::open(file_path.c_str(), O_RDONLY | O_NONBLOCK);
        if (fd < 0) {
            // 文件打开失败，发送错误响应
            response.set_status(HTTPStatus::InternalServerError);
            response.set_body("Failed to open file");
            auto response_buffer = response.serialize();
            return conn->socket()->write_async(response_buffer).then([](size_t) {});
        }
        
        // 获取文件大小
        struct stat st;
        if (fstat(fd, &st) < 0) {
            ::close(fd);
            response.set_status(HTTPStatus::InternalServerError);
            response.set_body("Failed to get file size");
            auto response_buffer = response.serialize();
            return conn->socket()->write_async(response_buffer).then([](size_t) {});
        }
        
        size_t file_size = st.st_size;
        
        // 获取Range信息
        size_t start = response.stream_start();
        size_t end = response.stream_end();
        if (end >= file_size) {
            end = file_size - 1;
        }
        size_t content_length = end - start + 1;
        
        // 定位到起始位置
        if (lseek(fd, start, SEEK_SET) < 0) {
            ::close(fd);
            response.set_status(HTTPStatus::InternalServerError);
            response.set_body("Failed to seek in file");
            auto response_buffer = response.serialize();
            return conn->socket()->write_async(response_buffer).then([](size_t) {});
        }
        
        // 序列化响应头（不包括body）
        auto response_buffer = response.serialize();
        
        // 更新统计
        stats_.bytes_sent.fetch_add(response_buffer.size() + content_length, std::memory_order_relaxed);
        stats_.status_2xx.fetch_add(1, std::memory_order_relaxed);
        
        // 保存文件流状态
        auto& stream_state = conn->file_stream_state();
        stream_state.fd = fd;
        stream_state.file_path = file_path;
        stream_state.file_size = file_size;
        stream_state.bytes_sent = 0;
        stream_state.bytes_to_send = content_length;
        stream_state.active = true;
        
        // 发送响应头，然后开始流式传输文件
        auto send_stream = [this, conn, remote_addr](size_t header_sent) {
            (void)header_sent;
            send_file_stream_async(conn, remote_addr);
        };
        return conn->socket()->write_async(response_buffer).then(send_stream);
    }
    
    // Serialize response
    auto response_buffer = response.serialize();
    
    // Update statistics
    stats_.bytes_sent.fetch_add(response_buffer.size(), std::memory_order_relaxed);
    
    // Update status code statistics
    int status_code = static_cast<int>(response.status());
    if (status_code >= 200 && status_code < 300) {
        stats_.status_2xx.fetch_add(1, std::memory_order_relaxed);
    } else if (status_code >= 300 && status_code < 400) {
        stats_.status_3xx.fetch_add(1, std::memory_order_relaxed);
    } else if (status_code >= 400 && status_code < 500) {
        stats_.status_4xx.fetch_add(1, std::memory_order_relaxed);
    } else if (status_code >= 500) {
        stats_.status_5xx.fetch_add(1, std::memory_order_relaxed);
    }
    
    // Check keep-alive
    conn->set_keep_alive(request->keep_alive() && response.status() != HTTPStatus::BadRequest);
    
    // Asynchronously send response
    return conn->socket()->write_async(response_buffer).then(
        [conn, remote_addr](size_t bytes_sent) {
            try {
                printf("DEBUG: write_async then callback called, bytes_sent=%zu\n", bytes_sent);
                // fflush(stdout);
                (void)bytes_sent;
            } catch (const std::exception& e) {
                printf("DEBUG: Exception in write_async then callback: %s\n", e.what());
                // fflush(stdout);
            } catch (...) {
                printf("DEBUG: Unknown exception in write_async then callback\n");
                // fflush(stdout);
            }
        });
}

future::Future<void> HTTPServer::close_connection_async(const std::string& remote_addr) {
    return future::make_ready_future().then([this, remote_addr]() {
        std::shared_ptr<HTTPConnection> conn;
        {
            std::lock_guard<std::mutex> lock(connections_mutex_);
            auto it = connections_.find(remote_addr);
            if (it != connections_.end()) {
                conn = it->second;
                connections_.erase(it);
            }
        }
        
        if (conn) {
            conn->close();
            stats_.active_connections.fetch_sub(1, std::memory_order_relaxed);
            // 立即清理conn，防止异步回调访问已释放的资源
            conn.reset();
        }
    });
}

void HTTPServer::handle_request(HTTPRequest& request, HTTPResponse& response) {
    try {
        route_request(request, response);
    } catch (const std::exception& e) {
        if (error_handler_) {
            error_handler_(request, response);
        }
    }
}

void HTTPServer::apply_middleware(HTTPRequest& request, HTTPResponse& response, size_t middleware_index) {
    if (middleware_index < middleware_.size()) {
        // Execute middleware
        middleware_[middleware_index](request, response, [this, &request, &response, middleware_index]() {
            apply_middleware(request, response, middleware_index + 1);
        });
    } else {
        // All middleware executed, handle request
        handle_request(request, response);
    }
}

void HTTPServer::route_request(HTTPRequest& request, HTTPResponse& response) {
    std::string method_str = method_to_string(request.method());
    std::string path = request.path();
    
    // Lock-free lookup using RCU
    HTTPRequestHandler handler;
    if (route_table_.lookup(method_str, path, handler)) {
        handler(request, response);
        return;
    }
    
    // No matching route found
    if (not_found_handler_) {
        not_found_handler_(request, response);
    }
}

future::Future<void> HTTPServer::send_file_stream_async(
    std::shared_ptr<HTTPConnection> conn,
    const std::string& remote_addr) {
    
    auto& stream_state = conn->file_stream_state();
    
    if (!stream_state.active || stream_state.fd < 0) {
        return close_connection_async(remote_addr);
    }
    
    // 使用64K分块读取文件
    constexpr size_t CHUNK_SIZE = 64 * 1024; // 64KB
    std::vector<char> buffer(CHUNK_SIZE);
    
    // 计算本次需要读取的字节数
    size_t remaining = stream_state.bytes_to_send - stream_state.bytes_sent;
    size_t bytes_to_read = std::min(CHUNK_SIZE, remaining);
    
    // 读取文件（非阻塞模式）
    ssize_t bytes_read = read(stream_state.fd, buffer.data(), bytes_to_read);
    
    if (bytes_read < 0) {
        // 读取错误
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            // 暂时没有数据，稍后重试
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            return send_file_stream_async(conn, remote_addr);
        } else {
            // 其他错误
            if (stream_state.fd >= 0) {
                ::close(stream_state.fd);
                stream_state.fd = -1;
            }
            stream_state.active = false;
            return close_connection_async(remote_addr);
        }
    } else if (bytes_read == 0) {
        // 文件已读完（EOF）
        if (stream_state.fd >= 0) {
            ::close(stream_state.fd);
            stream_state.fd = -1;
        }
        stream_state.active = false;

        // 删除下载锁文件（如果存在）并更新文件修改时间
        std::string download_lock = stream_state.file_path + ".download";
        std::error_code ec;
        if (std::filesystem::exists(download_lock, ec)) {
            std::filesystem::remove(download_lock, ec);
            std::filesystem::last_write_time(stream_state.file_path, std::filesystem::file_time_type::clock::now(), ec);
        }

        return close_connection_async(remote_addr);
    }

    // 创建数据块
    memory::ZeroCopyBuffer chunk(buffer.data(), bytes_read);    
    // 异步发送数据块
    auto send_next = [this, conn, remote_addr](size_t bytes_sent) {
        auto& state = conn->file_stream_state();
        if (!state.active) {
            close_connection_async(remote_addr);
            return;
        }
        
        // 更新活动时间，防止连接被超时断开
        conn->update_activity_time();
        
        // 更新已发送字节数
        state.bytes_sent += bytes_sent;
        
        // 检查是否传输完成
        if (state.bytes_sent >= state.bytes_to_send) {
            // 传输完成
            if (state.fd >= 0) {
                ::close(state.fd);
                state.fd = -1;
            }
            state.active = false;
            
            // 删除下载锁文件（如果存在）并更新文件修改时间
            std::string download_lock = state.file_path + ".download";
            std::error_code ec;
            if (std::filesystem::exists(download_lock, ec)) {
                std::filesystem::remove(download_lock, ec);
                std::filesystem::last_write_time(state.file_path, std::filesystem::file_time_type::clock::now(), ec);
            }
            
            close_connection_async(remote_addr);
        } else {
            // 继续发送下一块
            send_file_stream_async(conn, remote_addr);
        }
    };
    return conn->socket()->write_async(chunk).then(send_next);
}

void HTTPServer::cleanup_idle_connections() {
    std::lock_guard<std::mutex> lock(connections_mutex_);
    
    auto now = std::chrono::steady_clock::now().time_since_epoch().count();
    
    for (auto it = connections_.begin(); it != connections_.end(); ) {
        auto& [addr, conn] = *it;
        
        uint64_t idle_time = now - conn->last_activity_time();
        if (idle_time > config_.keep_alive_timeout_ms * 1000000ULL) {
            conn->close();
            it = connections_.erase(it);
            stats_.active_connections--;
        } else {
            ++it;
        }
    }
}

// Route registration
void HTTPServer::get(const std::string& path, HTTPRequestHandler handler) {
    std::lock_guard<std::mutex> lock(pending_routes_mutex_);
    pending_routes_.push_back({"GET", path, handler});
    rebuild_route_table();
}

void HTTPServer::post(const std::string& path, HTTPRequestHandler handler) {
    std::lock_guard<std::mutex> lock(pending_routes_mutex_);
    pending_routes_.push_back({"POST", path, handler});
    rebuild_route_table();
}

void HTTPServer::put(const std::string& path, HTTPRequestHandler handler) {
    std::lock_guard<std::mutex> lock(pending_routes_mutex_);
    pending_routes_.push_back({"PUT", path, handler});
    rebuild_route_table();
}

void HTTPServer::del(const std::string& path, HTTPRequestHandler handler) {
    std::lock_guard<std::mutex> lock(pending_routes_mutex_);
    pending_routes_.push_back({"DELETE", path, handler});
    rebuild_route_table();
}

void HTTPServer::patch(const std::string& path, HTTPRequestHandler handler) {
    std::lock_guard<std::mutex> lock(pending_routes_mutex_);
    pending_routes_.push_back({"PATCH", path, handler});
    rebuild_route_table();
}

void HTTPServer::head(const std::string& path, HTTPRequestHandler handler) {
    std::lock_guard<std::mutex> lock(pending_routes_mutex_);
    pending_routes_.push_back({"HEAD", path, handler});
    rebuild_route_table();
}

void HTTPServer::options(const std::string& path, HTTPRequestHandler handler) {
    std::lock_guard<std::mutex> lock(pending_routes_mutex_);
    pending_routes_.push_back({"OPTIONS", path, handler});
    rebuild_route_table();
}

void HTTPServer::any(const std::string& path, HTTPRequestHandler handler) {
    std::lock_guard<std::mutex> lock(pending_routes_mutex_);
    pending_routes_.push_back({"ANY", path, handler});
    rebuild_route_table();
}

void HTTPServer::rebuild_route_table() {
    // Create new route map (RCU pattern)
    auto new_routes = std::make_shared<LockFreeRouteTable::RouteMap>();
    
    // Build exact routes first (for fast path)
    for (const auto& reg : pending_routes_) {
        LockFreeRouteTable::Route route;
        route.path = reg.path;
        route.handler = reg.handler;
        route.is_pattern = reg.path.find('{') != std::string::npos;
        if (route.is_pattern) {
            route.pattern = std::regex(std::regex_replace(reg.path, std::regex("\\{[^}]+\\}"), "([^/]+)"));
        }
        
        if (reg.method == "ANY") {
            new_routes->any_routes["ANY"].push_back(route);
        } else {
            new_routes->routes_by_method[reg.method].push_back(route);
        }
    }
    
    // Atomic update (RCU)
    route_table_.update_routes(new_routes);
}

// Middleware
void HTTPServer::use(HTTPMiddleware middleware) {
    middleware_.push_back(middleware);
}

// Error handlers
void HTTPServer::set_not_found_handler(HTTPRequestHandler handler) {
    not_found_handler_ = handler;
}

void HTTPServer::set_error_handler(HTTPRequestHandler handler) {
    error_handler_ = handler;
}

// HTTPS
void HTTPServer::enable_https(const std::string& cert_file, const std::string& key_file) {
    https_enabled_ = true;
    cert_file_ = cert_file;
    key_file_ = key_file;
}

// AsyncHTTPServer implementation
AsyncHTTPServer::AsyncHTTPServer()
    : server_(std::make_unique<HTTPServer>())
{
}

AsyncHTTPServer::~AsyncHTTPServer() {
    stop();
}

bool AsyncHTTPServer::start(const HTTPServerConfig& config) {
    return server_->start(config);
}

void AsyncHTTPServer::stop() {
    server_->stop();
}

void AsyncHTTPServer::get(const std::string& path, AsyncHandler handler) {
    server_->get(path, [handler, this](HTTPRequest& req, HTTPResponse& res) {
        HTTPContext ctx(&req, &res, server_.get());
        // Simplified: call handler and ignore future for now
        // Future handling will be implemented later
        (void)handler(ctx);
    });
}

void AsyncHTTPServer::post(const std::string& path, AsyncHandler handler) {
    server_->post(path, [handler, this](HTTPRequest& req, HTTPResponse& res) {
        HTTPContext ctx(&req, &res, server_.get());
        (void)handler(ctx);
    });
}

void AsyncHTTPServer::put(const std::string& path, AsyncHandler handler) {
    server_->put(path, [handler, this](HTTPRequest& req, HTTPResponse& res) {
        HTTPContext ctx(&req, &res, server_.get());
        (void)handler(ctx);
    });
}

void AsyncHTTPServer::del(const std::string& path, AsyncHandler handler) {
    server_->del(path, [handler, this](HTTPRequest& req, HTTPResponse& res) {
        HTTPContext ctx(&req, &res, server_.get());
        (void)handler(ctx);
    });
}

void AsyncHTTPServer::any(const std::string& path, AsyncHandler handler) {
    server_->any(path, [handler, this](HTTPRequest& req, HTTPResponse& res) {
        HTTPContext ctx(&req, &res, server_.get());
        (void)handler(ctx);
    });
}

} // namespace network
} // namespace best_server