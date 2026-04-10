#include <iostream>
#include <string>
#include <cstring>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <openssl/ssl.h>
#include <openssl/err.h>
#include <thread>
#include <vector>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <csignal>

// Forward declaration
class HTTPSProxy;

// Global pointer for signal handling
HTTPSProxy* g_proxy = nullptr;

class HTTPSProxy {
private:
    SSL_CTX* ctx_;
    int listen_fd_;
    int http_port_;
    std::string http_host_;
    std::atomic<bool> running_;
    
    // Thread pool
    std::vector<std::thread> worker_threads_;
    std::queue<int> task_queue_;
    std::mutex queue_mutex_;
    std::condition_variable queue_cv_;
    static const int MAX_THREADS = 200;
    static const int MAX_PENDING_CONNECTIONS = 1024;
    std::atomic<int> active_connections_;

public:
    HTTPSProxy(int port, const std::string& cert_file, const std::string& key_file, 
               int http_port, const std::string& http_host)
        : listen_fd_(-1), http_port_(http_port), http_host_(http_host), running_(false), active_connections_(0) {
        
        // Initialize OpenSSL
        SSL_library_init();
        SSL_load_error_strings();
        OpenSSL_add_all_algorithms();
        
        // Create SSL context
        const SSL_METHOD* method = TLS_server_method();
        ctx_ = SSL_CTX_new(method);
        if (!ctx_) {
            std::cerr << "Failed to create SSL context" << std::endl;
            ERR_print_errors_fp(stderr);
            return;
        }
        
        // Load certificate and private key
        if (SSL_CTX_use_certificate_file(ctx_, cert_file.c_str(), SSL_FILETYPE_PEM) <= 0) {
            std::cerr << "Failed to load certificate" << std::endl;
            ERR_print_errors_fp(stderr);
            SSL_CTX_free(ctx_);
            ctx_ = nullptr;
            return;
        }
        
        if (SSL_CTX_use_PrivateKey_file(ctx_, key_file.c_str(), SSL_FILETYPE_PEM) <= 0) {
            std::cerr << "Failed to load private key" << std::endl;
            ERR_print_errors_fp(stderr);
            SSL_CTX_free(ctx_);
            ctx_ = nullptr;
            return;
        }
        
        // Create listening socket (IPv6 dual-stack)
        listen_fd_ = socket(AF_INET6, SOCK_STREAM, 0);
        if (listen_fd_ < 0) {
            std::cerr << "Failed to create socket" << std::endl;
            SSL_CTX_free(ctx_);
            ctx_ = nullptr;
            return;
        }
        
        // Set socket options
        int opt = 1;
        setsockopt(listen_fd_, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
        
        // Disable IPV6_V6ONLY to allow IPv4 connections
        int v6only = 0;
        setsockopt(listen_fd_, IPPROTO_IPV6, IPV6_V6ONLY, &v6only, sizeof(v6only));
        
        // Bind to address (IPv6 any address)
        struct sockaddr_in6 addr;
        memset(&addr, 0, sizeof(addr));
        addr.sin6_family = AF_INET6;
        addr.sin6_addr = in6addr_any;
        addr.sin6_port = htons(port);
        
        if (bind(listen_fd_, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
            std::cerr << "Failed to bind socket" << std::endl;
            close(listen_fd_);
            SSL_CTX_free(ctx_);
            ctx_ = nullptr;
            return;
        }
        
        // Listen with larger backlog for handling many concurrent connections
        if (listen(listen_fd_, MAX_PENDING_CONNECTIONS) < 0) {
            std::cerr << "Failed to listen" << std::endl;
            close(listen_fd_);
            SSL_CTX_free(ctx_);
            ctx_ = nullptr;
            return;
        }
        
        // Set running flag before creating worker threads
        running_ = true;
        
        // Initialize thread pool
        for (int i = 0; i < MAX_THREADS; ++i) {
            worker_threads_.emplace_back([this]() { worker_thread(); });
        }
        
        std::cout << "HTTPS proxy started on port " << port << std::endl;
        std::cout << "Forwarding to HTTP://" << http_host_ << ":" << http_port_ << std::endl;
        std::cout << "Thread pool size: " << MAX_THREADS << std::endl;
        std::cout << "Max pending connections: " << MAX_PENDING_CONNECTIONS << std::endl;
    }
    
    ~HTTPSProxy() {
        stop();
        if (ctx_) {
            SSL_CTX_free(ctx_);
        }
    }
    
    void start() {
        if (!ctx_) {
            std::cerr << "Failed to initialize HTTPS proxy" << std::endl;
            return;
        }
        
        while (running_) {
            struct sockaddr_in6 client_addr;
            socklen_t client_len = sizeof(client_addr);
            
            int client_fd = accept(listen_fd_, (struct sockaddr*)&client_addr, &client_len);
            
            if (client_fd < 0) {
                if (running_) {
                    std::cerr << "Failed to accept connection" << std::endl;
                }
                continue;
            }
            
            // Check connection limit
            if (active_connections_.load() >= MAX_THREADS * 2) {
                std::cerr << "Connection limit reached, rejecting connection" << std::endl;
                close(client_fd);
                continue;
            }
            
            // Add connection to thread pool
            {
                std::lock_guard<std::mutex> lock(queue_mutex_);
                task_queue_.push(client_fd);
            }
            queue_cv_.notify_one();
        }
    }
    
    void stop() {
        running_ = false;
        
        // Wake up all worker threads
        {
            std::lock_guard<std::mutex> lock(queue_mutex_);
            while (!task_queue_.empty()) {
                int fd = task_queue_.front();
                task_queue_.pop();
                close(fd);
            }
        }
        queue_cv_.notify_all();
        
        // Wait for worker threads to finish
        for (auto& thread : worker_threads_) {
            if (thread.joinable()) {
                thread.join();
            }
        }
        
        if (listen_fd_ >= 0) {
            close(listen_fd_);
            listen_fd_ = -1;
        }
    }
    
private:
    void worker_thread() {
        while (running_) {
            int client_fd = -1;
            
            // Wait for task
            {
                std::unique_lock<std::mutex> lock(queue_mutex_);
                queue_cv_.wait(lock, [this]() { 
                    return !task_queue_.empty() || !running_; 
                });
                
                if (!running_) {
                    break;
                }
                
                if (task_queue_.empty()) {
                    continue;
                }
                
                client_fd = task_queue_.front();
                task_queue_.pop();
            }
            
            if (client_fd >= 0) {
                active_connections_++;
                handle_client(client_fd);
                active_connections_--;
            }
        }
    }
    
    void handle_client(int client_fd) {
        // Create SSL connection
        SSL* ssl = SSL_new(ctx_);
        if (!ssl) {
            std::cerr << "Failed to create SSL structure" << std::endl;
            close(client_fd);
            return;
        }
        
        SSL_set_fd(ssl, client_fd);
        
        // Perform SSL handshake
        if (SSL_accept(ssl) <= 0) {
            std::cerr << "SSL handshake failed" << std::endl;
            ERR_print_errors_fp(stderr);
            SSL_free(ssl);
            close(client_fd);
            return;
        }
        
        // Read request from client (使用256KB缓冲区)
        std::vector<char> buffer(256 * 1024);
        int bytes_read = SSL_read(ssl, buffer.data(), buffer.size() - 1);
        if (bytes_read <= 0) {
            std::cerr << "Failed to read from SSL connection" << std::endl;
            SSL_shutdown(ssl);
            SSL_free(ssl);
            close(client_fd);
            return;
        }

        std::string request(buffer.data(), bytes_read);

        // Check if this is a HEAD request
        bool is_head_request = (request.compare(0, 4, "HEAD") == 0);

        // Check if this is a POST request with body (case-insensitive)
        std::string request_lower = request;
        std::transform(request_lower.begin(), request_lower.end(), request_lower.begin(), ::tolower);
        size_t content_length_pos = request_lower.find("content-length:");
        if (content_length_pos != std::string::npos) {
            size_t value_start = content_length_pos + 16;
            size_t value_end = request.find("\r\n", value_start);
            std::string length_str = request.substr(value_start, value_end - value_start);
            try {
                size_t content_length = std::stoull(length_str);
                size_t body_size = request.size() - request.find("\r\n\r\n") - 4;

                // Read remaining body if not complete
                if (body_size < content_length) {
                    size_t remaining = content_length - body_size;
                    while (remaining > 0) {
                        size_t chunk_size = std::min(remaining, buffer.size());
                        int additional_read = SSL_read(ssl, buffer.data(), chunk_size);
                        if (additional_read <= 0) {
                            break;
                        }
                        request.append(buffer.data(), additional_read);
                        remaining -= additional_read;
                    }
                }
            } catch (...) {
                // Invalid Content-Length, ignore
            }
        }
        
        // Forward request to HTTP server
        int http_fd = connect_to_http_server();
        if (http_fd < 0) {
            SSL_shutdown(ssl);
            SSL_free(ssl);
            close(client_fd);
            return;
        }
        
        // Set send timeout for large file uploads (60 minutes)
        struct timeval send_timeout;
        send_timeout.tv_sec = 3600;  // 60 minutes send timeout
        send_timeout.tv_usec = 0;
        setsockopt(http_fd, SOL_SOCKET, SO_SNDTIMEO, &send_timeout, sizeof(send_timeout));
        
        // Send request to HTTP server
        size_t total_sent = 0;
        size_t request_size = request.size();
        while (total_sent < request_size) {
            ssize_t sent = write(http_fd, request.c_str() + total_sent, request_size - total_sent);
            if (sent < 0) {
                std::cerr << "Failed to send request to HTTP server" << std::endl;
                close(http_fd);
                SSL_shutdown(ssl);
                SSL_free(ssl);
                close(client_fd);
                return;
            }
            total_sent += sent;
        }
        
        // Read response headers from HTTP server (使用256KB缓冲区)
        std::string response;
        std::vector<char> resp_buffer(256 * 1024);
        int bytes_read_http;
        
        // Set socket timeout to prevent blocking (60 minutes for file downloads)
        struct timeval timeout;
        timeout.tv_sec = 3600;  // 60 minutes timeout
        timeout.tv_usec = 0;
        setsockopt(http_fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
        
        // Track response state
        bool headers_complete = false;
        size_t header_end = 0;
        int64_t content_length = -1;
        bool chunked_encoding = false;
        
        // Read response headers
        while (!headers_complete && (bytes_read_http = read(http_fd, resp_buffer.data(), resp_buffer.size())) > 0) {
            response.append(resp_buffer.data(), bytes_read_http);
            
            // Check if we have received complete headers
            if (response.find("\r\n\r\n") != std::string::npos) {
                headers_complete = true;
                header_end = response.find("\r\n\r\n");
                std::string headers = response.substr(0, header_end);
                
                // Check for Content-Length (case-insensitive)
                std::string headers_lower = headers;
                std::transform(headers_lower.begin(), headers_lower.end(), headers_lower.begin(), ::tolower);
                size_t cl_pos = headers_lower.find("content-length:");
                if (cl_pos != std::string::npos) {
                    size_t value_start = cl_pos + 16;
                    size_t value_end = headers.find("\r\n", value_start);
                    std::string length_str = headers.substr(value_start, value_end - value_start);
                    try {
                        content_length = std::stoll(length_str);
                    } catch (...) {
                        content_length = -1;
                    }
                }
                
                // Check for Transfer-Encoding: chunked
                if (headers.find("Transfer-Encoding: chunked") != std::string::npos) {
                    chunked_encoding = true;
                }
            }
        }
        
        // Send headers to client
        if (headers_complete) {
            // Send headers
            if (SSL_write(ssl, response.c_str(), header_end + 4) <= 0) {
                std::cerr << "Failed to send headers to client" << std::endl;
                ERR_print_errors_fp(stderr);
                close(http_fd);
                SSL_shutdown(ssl);
                SSL_free(ssl);
                close(client_fd);
                return;
            }
            
            // If HEAD request, don't send body
            if (is_head_request) {
                close(http_fd);
                SSL_shutdown(ssl);
                SSL_free(ssl);
                close(client_fd);
                return;
            }
            
            // Stream body to client
            size_t body_size = response.size() - header_end - 4;

            // Send any body data already received
            if (body_size > 0) {
                if (SSL_write(ssl, response.c_str() + header_end + 4, body_size) <= 0) {
                    std::cerr << "Failed to send body to client" << std::endl;
                    close(http_fd);
                    SSL_shutdown(ssl);
                    SSL_free(ssl);
                    close(client_fd);
                    return;
                }
            }

            // Continue streaming body data
            if (content_length >= 0) {
                // Known content length - read exact amount
                int64_t bytes_to_read = content_length - body_size;
                while (bytes_to_read > 0 && (bytes_read_http = read(http_fd, resp_buffer.data(), std::min((size_t)bytes_to_read, resp_buffer.size()))) > 0) {
                    if (SSL_write(ssl, resp_buffer.data(), bytes_read_http) <= 0) {
                        std::cerr << "Failed to send body to client" << std::endl;
                        break;
                    }
                    bytes_to_read -= bytes_read_http;
                }
            } else if (chunked_encoding) {
                // Chunked encoding - read until 0\r\n\r\n
                while ((bytes_read_http = read(http_fd, resp_buffer.data(), resp_buffer.size())) > 0) {
                    if (SSL_write(ssl, resp_buffer.data(), bytes_read_http) <= 0) {
                        std::cerr << "Failed to send body to client" << std::endl;
                        break;
                    }
                    // Check for end chunk
                    std::string received(resp_buffer.data(), bytes_read_http);
                    if (received.find("0\r\n\r\n") != std::string::npos) {
                        break;
                    }
                }
            } else {
                // No content length - read until connection closes
                while ((bytes_read_http = read(http_fd, resp_buffer.data(), resp_buffer.size())) > 0) {
                    if (SSL_write(ssl, resp_buffer.data(), bytes_read_http) <= 0) {
                        std::cerr << "Failed to send body to client" << std::endl;
                        break;
                    }
                }
            }
        } else {
            // No headers received, send whatever we got
            if (SSL_write(ssl, response.c_str(), response.size()) <= 0) {
                std::cerr << "Failed to send response to client" << std::endl;
                ERR_print_errors_fp(stderr);
            }
        }
        
        close(http_fd);
        SSL_shutdown(ssl);
        SSL_free(ssl);
        close(client_fd);
    }
    
    int connect_to_http_server() {
        int fd = socket(AF_INET, SOCK_STREAM, 0);
        if (fd < 0) {
            std::cerr << "Failed to create socket for HTTP connection" << std::endl;
            return -1;
        }
        
        struct hostent* host = gethostbyname(http_host_.c_str());
        if (!host) {
            std::cerr << "Failed to resolve HTTP host" << std::endl;
            close(fd);
            return -1;
        }
        
        struct sockaddr_in addr;
        memset(&addr, 0, sizeof(addr));
        addr.sin_family = AF_INET;
        addr.sin_port = htons(http_port_);
        memcpy(&addr.sin_addr, host->h_addr, host->h_length);
        
        if (connect(fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
            std::cerr << "Failed to connect to HTTP server" << std::endl;
            close(fd);
            return -1;
        }
        
        return fd;
    }
};

// Signal handler function (defined after class to avoid forward declaration issues)
void signal_handler(int signal) {
    std::cout << "\nReceived signal " << signal << ", shutting down..." << std::endl;
    if (g_proxy) {
        g_proxy->stop();
    }
}

int main(int argc, char* argv[]) {
    // Set up signal handlers
    std::signal(SIGINT, signal_handler);
    std::signal(SIGTERM, signal_handler);
    std::signal(SIGPIPE, SIG_IGN);  // Ignore SIGPIPE
    
    if (argc < 6) {
        std::cerr << "Usage: " << argv[0] << " <https_port> <cert_file> <key_file> <http_port> <http_host>" << std::endl;
        std::cerr << "Example: " << argv[0] << " 8443 cert.pem key.pem 8080 127.0.0.1" << std::endl;
        return 1;
    }
    
    int https_port = std::stoi(argv[1]);
    std::string cert_file = argv[2];
    std::string key_file = argv[3];
    int http_port = std::stoi(argv[4]);
    std::string http_host = argv[5];
    
    HTTPSProxy proxy(https_port, cert_file, key_file, http_port, http_host);
    g_proxy = &proxy;
    proxy.start();
    
    std::cout << "HTTPS proxy stopped" << std::endl;
    return 0;
}