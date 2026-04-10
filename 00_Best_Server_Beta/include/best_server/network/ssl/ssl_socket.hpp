#pragma once

#include <best_server/io/tcp_socket.hpp>
#include <best_server/future/future.hpp>
#include <memory>
#include <string>
#include <vector>

// Forward declarations for OpenSSL types
struct ssl_st;
struct ssl_ctx_st;
struct x509_st;

namespace best_server {
namespace network {
namespace ssl {

// SSL/TLS configuration
struct SSLConfig {
    std::string cert_file;          // Server certificate file
    std::string key_file;           // Private key file
    std::string ca_file;            // CA certificate file
    std::string ca_path;            // CA certificate directory
    
    bool verify_peer{false};        // Verify peer certificate
    bool verify_fail_if_no_peer_cert{false};
    bool verify_client_once{true};
    
    std::string cipher_list;        // Cipher list (empty for default)
    int min_version{0};             // Minimum TLS version
    int max_version{0};             // Maximum TLS version
    
    bool enable_session_cache{true};
    int session_timeout{300};       // Session timeout in seconds
    
    std::string alpn_protocols;     // ALPN protocols
};

// SSL error codes
enum class SSLError {
    None = 0,
    WantRead = 1,
    WantWrite = 2,
    ZeroReturn = 3,
    Syscall = 4,
    Connect = 5,
    Certificate = 6,
    Verify = 7
};

// SSL result
struct SSLResult {
    bool success{false};
    SSLError error{SSLError::None};
    std::string message;
    
    explicit operator bool() const { return success; }
};

// SSL context wrapper
class SSLContext {
public:
    using Ptr = std::shared_ptr<SSLContext>;
    
    static Ptr create_server(const SSLConfig& config);
    static Ptr create_client(const SSLConfig& config);
    
    ~SSLContext();
    
    ssl_ctx_st* native_handle() const { return ctx_; }
    
private:
    SSLContext();
    bool init(const SSLConfig& config, bool is_server);
    
    ssl_ctx_st* ctx_{nullptr};
};

// SSL socket wrapper
class SSLSocket {
public:
    using Ptr = std::shared_ptr<SSLSocket>;
    
    // Create SSL socket from TCP socket
    static Ptr wrap(io::TCPSocket::Ptr socket, SSLContext::Ptr context);
    
    // Connect with SSL
    static future::Future<Ptr> connect(const std::string& host, uint16_t port,
                                       const SSLConfig& config);
    
    ~SSLSocket();
    
    // SSL handshake
    future::Future<SSLResult> handshake();
    future::Future<SSLResult> accept();
    
    // SSL read/write
    future::Future<SSLResult> read_async(std::vector<uint8_t>& buffer, size_t max_size);
    future::Future<SSLResult> write_async(const std::vector<uint8_t>& data);
    
    // Shutdown SSL
    future::Future<SSLResult> shutdown();
    
    // Get underlying TCP socket
    io::TCPSocket::Ptr tcp_socket() const { return socket_; }
    
    // Get peer certificate
    std::string peer_certificate() const;
    bool verify_peer() const;
    
    // Get cipher info
    std::string cipher_name() const;
    std::string cipher_version() const;
    int cipher_bits() const;
    
    // Get protocol version
    std::string protocol_version() const;
    
    // Get session info
    std::string session_id() const;
    
    // Close socket
    void close();
    
private:
    SSLSocket(io::TCPSocket::Ptr socket, SSLContext::Ptr context);
    bool init();
    
    io::TCPSocket::Ptr socket_;
    SSLContext::Ptr context_;
    ssl_st* ssl_{nullptr};
    bool handshake_complete_{false};
};

// SSL utilities
namespace utils {
    // Convert SSL error to string
    std::string error_string(SSLError error);
    
    // Get certificate info
    std::string certificate_info(x509_st* cert);
    std::string certificate_subject(x509_st* cert);
    std::string certificate_issuer(x509_st* cert);
    std::string certificate_serial(x509_st* cert);
    std::chrono::system_clock::time_point certificate_not_before(x509_st* cert);
    std::chrono::system_clock::time_point certificate_not_after(x509_st* cert);
    
    // Verify certificate
    bool verify_certificate(x509_st* cert, const std::string& hostname);
    
    // Generate self-signed certificate (for testing)
    bool generate_self_signed_cert(const std::string& cert_file,
                                   const std::string& key_file,
                                   const std::string& common_name,
                                   int days = 365);
}

} // namespace ssl
} // namespace network
} // namespace best_server