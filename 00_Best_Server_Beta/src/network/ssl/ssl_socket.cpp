// SSL/TLS Socket Implementation
// Based on OpenSSL library

#include <best_server/network/ssl/ssl_socket.hpp>
#include <best_server/core/reactor.hpp>
#include <best_server/core/scheduler.hpp>
#include <openssl/ssl.h>
#include <openssl/err.h>
#include <openssl/x509.h>
#include <openssl/x509v3.h>
#include <openssl/rand.h>
#include <openssl/pem.h>
#include <sys/select.h>
#include <chrono>
#include <cstring>

namespace best_server {
namespace network {
namespace ssl {

namespace {
    // Thread-safe OpenSSL initialization
    class OpenSSLInit {
    public:
        OpenSSLInit() {
            SSL_library_init();
            SSL_load_error_strings();
            OpenSSL_add_all_algorithms();
            
            // Random seed
            unsigned char seed[32];
            RAND_bytes(seed, sizeof(seed));
        }
        
        ~OpenSSLInit() {
            EVP_cleanup();
            ERR_free_strings();
        }
    };
    
    static OpenSSLInit openssl_init_;
}

// SSLContext implementation
SSLContext::SSLContext() = default;

SSLContext::~SSLContext() {
    if (ctx_) {
        SSL_CTX_free(ctx_);
        ctx_ = nullptr;
    }
}

SSLContext::Ptr SSLContext::create_server(const SSLConfig& config) {
    auto ctx = std::shared_ptr<SSLContext>(new SSLContext());
    if (!ctx->init(config, true)) {
        return nullptr;
    }
    return ctx;
}

SSLContext::Ptr SSLContext::create_client(const SSLConfig& config) {
    auto ctx = std::shared_ptr<SSLContext>(new SSLContext());
    if (!ctx->init(config, false)) {
        return nullptr;
    }
    return ctx;
}

bool SSLContext::init(const SSLConfig& config, bool is_server) {
    // Create SSL context
    const SSL_METHOD* method = is_server ? 
        TLS_server_method() : TLS_client_method();
    
    ctx_ = SSL_CTX_new(method);
    if (!ctx_) {
        return false;
    }
    
    // Set verify mode
    int verify_mode = SSL_VERIFY_NONE;
    if (config.verify_peer) {
        verify_mode |= SSL_VERIFY_PEER;
    }
    if (config.verify_fail_if_no_peer_cert) {
        verify_mode |= SSL_VERIFY_FAIL_IF_NO_PEER_CERT;
    }
    if (config.verify_client_once) {
        verify_mode |= SSL_VERIFY_CLIENT_ONCE;
    }
    SSL_CTX_set_verify(ctx_, verify_mode, nullptr);
    
    // Load certificates
    if (!config.cert_file.empty()) {
        if (SSL_CTX_use_certificate_file(ctx_, config.cert_file.c_str(),
                                         SSL_FILETYPE_PEM) != 1) {
            return false;
        }
    }
    
    if (!config.key_file.empty()) {
        if (SSL_CTX_use_PrivateKey_file(ctx_, config.key_file.c_str(),
                                        SSL_FILETYPE_PEM) != 1) {
            return false;
        }
        
        // Verify private key
        if (SSL_CTX_check_private_key(ctx_) != 1) {
            return false;
        }
    }
    
    if (!config.ca_file.empty()) {
        if (SSL_CTX_load_verify_locations(ctx_, config.ca_file.c_str(),
                                           nullptr) != 1) {
            return false;
        }
    }
    
    if (!config.ca_path.empty()) {
        if (SSL_CTX_load_verify_locations(ctx_, nullptr,
                                           config.ca_path.c_str()) != 1) {
            return false;
        }
    }
    
    // Set cipher list
    if (!config.cipher_list.empty()) {
        if (SSL_CTX_set_cipher_list(ctx_, config.cipher_list.c_str()) != 1) {
            return false;
        }
    }
    
    // Set protocol versions
    if (config.min_version > 0) {
        SSL_CTX_set_min_proto_version(ctx_, config.min_version);
    }
    if (config.max_version > 0) {
        SSL_CTX_set_max_proto_version(ctx_, config.max_version);
    }
    
    // Session cache
    SSL_CTX_set_session_cache_mode(ctx_,
        config.enable_session_cache ? SSL_SESS_CACHE_SERVER : SSL_SESS_CACHE_OFF);
    SSL_CTX_set_timeout(ctx_, config.session_timeout);
    
    // ALPN
    if (!config.alpn_protocols.empty()) {
        SSL_CTX_set_alpn_protos(ctx_,
            reinterpret_cast<const unsigned char*>(config.alpn_protocols.c_str()),
            config.alpn_protocols.size());
    }
    
    // Auto retry on read/write
    SSL_CTX_set_mode(ctx_, SSL_MODE_AUTO_RETRY);
    SSL_CTX_set_mode(ctx_, SSL_MODE_ACCEPT_MOVING_WRITE_BUFFER);
    SSL_CTX_set_mode(ctx_, SSL_MODE_ENABLE_PARTIAL_WRITE);
    
    return true;
}

// SSLSocket implementation
SSLSocket::SSLSocket(io::TCPSocket::Ptr socket, SSLContext::Ptr context)
    : socket_(std::move(socket)), context_(std::move(context)) {
    init();
}

SSLSocket::~SSLSocket() {
    close();
}

SSLSocket::Ptr SSLSocket::wrap(io::TCPSocket::Ptr socket, SSLContext::Ptr context) {
    return std::shared_ptr<SSLSocket>(new SSLSocket(std::move(socket), std::move(context)));
}

future::Future<SSLSocket::Ptr> SSLSocket::connect(const std::string& host,
                                                     uint16_t port,
                                                     const SSLConfig& config) {
    return future::make_future<SSLSocket::Ptr>([host, port, config]() {
        // First establish TCP connection
        auto tcp_socket = io::TCPSocket::create();
        if (!tcp_socket->connect_sync(host, port)) {
            throw std::runtime_error("Failed to connect to " + host + ":" + std::to_string(port));
        }
        
        // Create SSL context
        auto context = SSLContext::create_client(config);
        if (!context) {
            throw std::runtime_error("Failed to create SSL context");
        }
        
        // Wrap TCP socket
        auto ssl_socket = std::shared_ptr<SSLSocket>(new SSLSocket(tcp_socket, context));
        
        // Set SNI hostname
        SSL_set_tlsext_host_name(ssl_socket->ssl_, host.c_str());
        
        // Perform handshake
        auto result = ssl_socket->handshake().get();
        if (!result.success) {
            throw std::runtime_error("SSL handshake failed: " + result.message);
        }
        
        return ssl_socket;
    });
}

bool SSLSocket::init() {
    if (!context_ || !socket_) {
        return false;
    }
    
    ssl_ = SSL_new(context_->native_handle());
    if (!ssl_) {
        return false;
    }
    
    // Set read/write callbacks
    SSL_set_fd(ssl_, static_cast<int>(socket_->native_handle()));
    
    return true;
}

future::Future<SSLResult> SSLSocket::handshake() {
    return future::make_future<SSLResult>([this]() {
        SSLResult result;
        result.success = true;
        
        while (true) {
            int ret = SSL_connect(ssl_);
            
            if (ret == 1) {
                handshake_complete_ = true;
                return result;
            }
            
            int error = SSL_get_error(ssl_, ret);
            
            if (error == SSL_ERROR_WANT_READ) {
                // Wait for socket to be readable
                socket_->wait_readable().get();
            } else if (error == SSL_ERROR_WANT_WRITE) {
                // Wait for socket to be writable
                socket_->wait_writable().get();
            } else {
                result.success = false;
                result.error = SSLError::Connect;
                result.message = utils::error_string(SSLError::Connect);
                return result;
            }
        }
    });
}

future::Future<SSLResult> SSLSocket::accept() {
    return future::make_future<SSLResult>([this]() {
        SSLResult result;
        result.success = true;
        
        while (true) {
            int ret = SSL_accept(ssl_);
            
            if (ret == 1) {
                handshake_complete_ = true;
                return result;
            }
            
            int error = SSL_get_error(ssl_, ret);
            
            if (error == SSL_ERROR_WANT_READ) {
                // Use select() instead of event loop
                fd_set read_fds;
                FD_ZERO(&read_fds);
                FD_SET(socket_->native_handle(), &read_fds);
                struct timeval timeout;
                timeout.tv_sec = 5;
                timeout.tv_usec = 0;
                select(socket_->native_handle() + 1, &read_fds, nullptr, nullptr, &timeout);
            } else if (error == SSL_ERROR_WANT_WRITE) {
                // Use select() instead of event loop
                fd_set write_fds;
                FD_ZERO(&write_fds);
                FD_SET(socket_->native_handle(), &write_fds);
                struct timeval timeout;
                timeout.tv_sec = 5;
                timeout.tv_usec = 0;
                select(socket_->native_handle() + 1, nullptr, &write_fds, nullptr, &timeout);
            } else {
                result.success = false;
                result.error = SSLError::Connect;
                result.message = utils::error_string(SSLError::Connect);
                return result;
            }
        }
    });
}

future::Future<SSLResult> SSLSocket::read_async(std::vector<uint8_t>& buffer,
                                                  size_t max_size) {
    return future::make_future<SSLResult>([this, &buffer, max_size]() {
        SSLResult result;
        result.success = true;
        
        buffer.resize(max_size);
        
        while (true) {
            int ret = SSL_read(ssl_, buffer.data(), static_cast<int>(max_size));
            
            if (ret > 0) {
                buffer.resize(ret);
                return result;
            }
            
            int error = SSL_get_error(ssl_, ret);
            
            if (error == SSL_ERROR_ZERO_RETURN) {
                // Connection closed
                result.error = SSLError::ZeroReturn;
                return result;
            } else if (error == SSL_ERROR_WANT_READ) {
                socket_->wait_readable().get();
            } else if (error == SSL_ERROR_WANT_WRITE) {
                socket_->wait_writable().get();
            } else {
                result.success = false;
                result.error = SSLError::Syscall;
                result.message = utils::error_string(SSLError::Syscall);
                return result;
            }
        }
    });
}

future::Future<SSLResult> SSLSocket::write_async(const std::vector<uint8_t>& data) {
    return future::make_future<SSLResult>([this, data]() {
        SSLResult result;
        result.success = true;
        
        size_t total_sent = 0;
        
        while (total_sent < data.size()) {
            int ret = SSL_write(ssl_,
                               data.data() + total_sent,
                               static_cast<int>(data.size() - total_sent));
            
            if (ret > 0) {
                total_sent += ret;
                continue;
            }
            
            int error = SSL_get_error(ssl_, ret);
            
            if (error == SSL_ERROR_WANT_READ) {
                socket_->wait_readable().get();
            } else if (error == SSL_ERROR_WANT_WRITE) {
                socket_->wait_writable().get();
            } else {
                result.success = false;
                result.error = SSLError::Syscall;
                result.message = utils::error_string(SSLError::Syscall);
                return result;
            }
        }
        
        return result;
    });
}

future::Future<SSLResult> SSLSocket::shutdown() {
    return future::make_future<SSLResult>([this]() {
        SSLResult result;
        result.success = true;
        
        int ret = SSL_shutdown(ssl_);
        
        if (ret == 1) {
            return result;
        }
        
        if (ret == 0) {
            // Wait for peer shutdown
            while (true) {
                ret = SSL_shutdown(ssl_);
                if (ret == 1) {
                    return result;
                }
                
                int error = SSL_get_error(ssl_, ret);
                if (error == SSL_ERROR_WANT_READ) {
                    socket_->wait_readable().get();
                } else if (error == SSL_ERROR_WANT_WRITE) {
                    socket_->wait_writable().get();
                } else {
                    break;
                }
            }
        }
        
        result.success = false;
        return result;
    });
}

std::string SSLSocket::peer_certificate() const {
    if (!ssl_) return "";
    
    X509* cert = SSL_get_peer_certificate(ssl_);
    if (!cert) return "";
    
    BIO* bio = BIO_new(BIO_s_mem());
    PEM_write_bio_X509(bio, cert);
    
    char* data = nullptr;
    long len = BIO_get_mem_data(bio, &data);
    std::string result(data, len);
    
    BIO_free(bio);
    X509_free(cert);
    
    return result;
}

bool SSLSocket::verify_peer() const {
    if (!ssl_) return false;
    
    long verify_result = SSL_get_verify_result(ssl_);
    return verify_result == X509_V_OK;
}

std::string SSLSocket::cipher_name() const {
    if (!ssl_) return "";
    
    const SSL_CIPHER* cipher = SSL_get_current_cipher(ssl_);
    if (!cipher) return "";
    
    return SSL_CIPHER_get_name(cipher);
}

std::string SSLSocket::cipher_version() const {
    if (!ssl_) return "";
    
    const SSL_CIPHER* cipher = SSL_get_current_cipher(ssl_);
    if (!cipher) return "";
    
    return SSL_CIPHER_get_version(cipher);
}

int SSLSocket::cipher_bits() const {
    if (!ssl_) return 0;
    
    const SSL_CIPHER* cipher = SSL_get_current_cipher(ssl_);
    if (!cipher) return 0;
    
    return SSL_CIPHER_get_bits(cipher, nullptr);
}

std::string SSLSocket::protocol_version() const {
    if (!ssl_) return "";
    
    const char* version = SSL_get_version(ssl_);
    return version ? version : "";
}

std::string SSLSocket::session_id() const {
    if (!ssl_) return "";
    
    SSL_SESSION* session = SSL_get_session(ssl_);
    if (!session) return "";
    
    unsigned int session_id_len = 0;
    const unsigned char* session_id_ptr = SSL_SESSION_get_id(session, &session_id_len);
    if (!session_id_ptr || session_id_len == 0) return "";
    
    std::string result;
    char buf[4];
    for (unsigned int i = 0; i < session_id_len; ++i) {
        snprintf(buf, sizeof(buf), "%02x", session_id_ptr[i]);
        result += buf;
    }
    
    return result;
}

void SSLSocket::close() {
    if (ssl_) {
        SSL_free(ssl_);
        ssl_ = nullptr;
    }
    
    // Don't close the TCP socket here - it's managed by the HTTPConnection
    // socket_->close();
    
    handshake_complete_ = false;
}

// Utility functions
namespace utils {

std::string error_string(SSLError error) {
    switch (error) {
        case SSLError::None:
            return "No error";
        case SSLError::WantRead:
            return "Want read";
        case SSLError::WantWrite:
            return "Want write";
        case SSLError::ZeroReturn:
            return "Connection closed";
        case SSLError::Syscall:
            return "System call error";
        case SSLError::Connect:
            return "Connection error";
        case SSLError::Certificate:
            return "Certificate error";
        case SSLError::Verify:
            return "Verification error";
        default:
            return "Unknown error";
    }
}

std::string certificate_info(x509_st* cert) {
    if (!cert) return "";
    
    BIO* bio = BIO_new(BIO_s_mem());
    X509_print(bio, cert);
    
    char* data = nullptr;
    long len = BIO_get_mem_data(bio, &data);
    std::string result(data, len);
    
    BIO_free(bio);
    return result;
}

std::string certificate_subject(x509_st* cert) {
    if (!cert) return "";
    
    char name[256];
    X509_NAME_oneline(X509_get_subject_name(cert), name, sizeof(name));
    return name;
}

std::string certificate_issuer(x509_st* cert) {
    if (!cert) return "";
    
    char name[256];
    X509_NAME_oneline(X509_get_issuer_name(cert), name, sizeof(name));
    return name;
}

std::string certificate_serial(x509_st* cert) {
    if (!cert) return "";
    
    ASN1_INTEGER* serial = X509_get_serialNumber(cert);
    BIGNUM* bn = ASN1_INTEGER_to_BN(serial, nullptr);
    char* hex = BN_bn2hex(bn);
    
    std::string result = hex;
    
    OPENSSL_free(hex);
    BN_free(bn);
    
    return result;
}

std::chrono::system_clock::time_point certificate_not_before(x509_st* cert) {
    if (!cert) return std::chrono::system_clock::time_point{};
    
    ASN1_TIME* time = X509_get_notBefore(cert);
    
    int day, sec;
    ASN1_TIME_diff(&day, &sec, nullptr, time);
    
    auto now = std::chrono::system_clock::now();
    return now - std::chrono::hours(day * 24) - std::chrono::seconds(sec);
}

std::chrono::system_clock::time_point certificate_not_after(x509_st* cert) {
    if (!cert) return std::chrono::system_clock::time_point{};
    
    ASN1_TIME* time = X509_get_notAfter(cert);
    
    int day, sec;
    ASN1_TIME_diff(&day, &sec, time, nullptr);
    
    auto now = std::chrono::system_clock::now();
    return now + std::chrono::hours(day * 24) + std::chrono::seconds(sec);
}

bool verify_certificate(x509_st* cert, const std::string& hostname) {
    if (!cert) return false;
    
    // Get hostname from certificate
    char hostname_buf[256];
    X509_NAME_get_text_by_NID(X509_get_subject_name(cert),
                              NID_commonName,
                              hostname_buf,
                              sizeof(hostname_buf));
    
    std::string cert_hostname = hostname_buf;
    
    // Simple hostname comparison (in production, use proper wildcard matching)
    return cert_hostname == hostname;
}

bool generate_self_signed_cert(const std::string& cert_file,
                               const std::string& key_file,
                               const std::string& common_name,
                               int days) {
    // Generate RSA key using OpenSSL 3.0 EVP API
    EVP_PKEY* pkey = nullptr;
    EVP_PKEY_CTX* ctx = EVP_PKEY_CTX_new_id(EVP_PKEY_RSA, nullptr);
    
    if (!ctx) {
        return false;
    }
    
    if (EVP_PKEY_keygen_init(ctx) <= 0 ||
        EVP_PKEY_CTX_set_rsa_keygen_bits(ctx, 2048) <= 0 ||
        EVP_PKEY_keygen(ctx, &pkey) <= 0) {
        EVP_PKEY_CTX_free(ctx);
        return false;
    }
    
    EVP_PKEY_CTX_free(ctx);
    
    // Create certificate
    X509* cert = X509_new();
    X509_set_version(cert, 2);
    
    ASN1_INTEGER_set(X509_get_serialNumber(cert), 1);
    
    X509_gmtime_adj(X509_get_notBefore(cert), 0);
    X509_gmtime_adj(X509_get_notAfter(cert), days * 24 * 60 * 60);
    
    X509_set_pubkey(cert, pkey);
    
    X509_NAME* name = X509_get_subject_name(cert);
    X509_NAME_add_entry_by_txt(name, "C", MBSTRING_ASC,
                               reinterpret_cast<const unsigned char*>("US"),
                               -1, -1, 0);
    X509_NAME_add_entry_by_txt(name, "CN", MBSTRING_ASC,
                               reinterpret_cast<const unsigned char*>(common_name.c_str()),
                               -1, -1, 0);
    
    X509_set_issuer_name(cert, name);
    
    X509_sign(cert, pkey, EVP_sha256());
    
    // Write certificate
    BIO* cert_bio = BIO_new_file(cert_file.c_str(), "w");
    PEM_write_bio_X509(cert_bio, cert);
    BIO_free(cert_bio);
    
    // Write private key
    BIO* key_bio = BIO_new_file(key_file.c_str(), "w");
    PEM_write_bio_PrivateKey(key_bio, pkey, nullptr, nullptr, 0, nullptr, nullptr);
    BIO_free(key_bio);
    
    X509_free(cert);
    EVP_PKEY_free(pkey);
    
    return true;
}

} // namespace utils

} // namespace ssl
} // namespace network
} // namespace best_server