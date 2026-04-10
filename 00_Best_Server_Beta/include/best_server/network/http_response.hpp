// HTTPResponse - HTTP response representation
// 
// Represents an HTTP response with:
// - Status code and message
// - Headers
// - Body
// - Cookies
// - Streaming support
// - Compression support

#ifndef BEST_SERVER_NETWORK_HTTP_RESPONSE_HPP
#define BEST_SERVER_NETWORK_HTTP_RESPONSE_HPP

#include <string>
#include <unordered_map>
#include <vector>
#include <memory>

#include "network/http_request.hpp"
#include "memory/zero_copy_buffer.hpp"

namespace best_server {
namespace network {

// HTTP status codes
enum class HTTPStatus : uint16_t {
    Continue = 100,
    SwitchingProtocols = 101,
    OK = 200,
    Created = 201,
    Accepted = 202,
    NoContent = 204,
    MovedPermanently = 301,
    Found = 302,
    NotModified = 304,
    PartialContent = 206,
    BadRequest = 400,
    Unauthorized = 401,
    Forbidden = 403,
    NotFound = 404,
    MethodNotAllowed = 405,
    RequestTimeout = 408,
    Conflict = 409,
    Gone = 410,
    PayloadTooLarge = 413,
    UnsupportedMediaType = 415,
    TooManyRequests = 429,
    InternalServerError = 500,
    NotImplemented = 501,
    BadGateway = 502,
    ServiceUnavailable = 503,
    GatewayTimeout = 504
};

// Get status message
std::string status_message(HTTPStatus status);

// HTTP response
class HTTPResponse {
public:
    HTTPResponse();
    explicit HTTPResponse(HTTPStatus status);
    ~HTTPResponse();
    
    // Status
    HTTPStatus status() const { return status_; }
    void set_status(HTTPStatus status) { status_ = status; }
    std::string status_message() const;
    
    // Version
    const HTTPVersion& version() const { return version_; }
    void set_version(const HTTPVersion& version) { version_ = version; }
    
    // Headers
    const std::unordered_map<std::string, std::string>& headers() const { return headers_; }
    void set_header(const std::string& name, const std::string& value);
    std::string get_header(const std::string& name, const std::string& default_value = "") const;
    bool has_header(const std::string& name) const;
    void remove_header(const std::string& name);
    
    // Body
    const memory::ZeroCopyBuffer& body() const { return body_; }
    memory::ZeroCopyBuffer& body() { return body_; }
    void set_body(memory::ZeroCopyBuffer&& body);
    void set_body(const std::string& body);
    const std::string& body_string() const;
    
    // Content type
    void set_content_type(const std::string& content_type);
    std::string content_type() const { return get_header("Content-Type", ""); }
    
    // Content length
    size_t content_length() const;
    
    // Cookies
    struct Cookie {
        std::string name;
        std::string value;
        std::string path;
        std::string domain;
        uint64_t max_age{0};
        bool secure{false};
        bool http_only{false};
        bool same_site{false};
    };
    void add_cookie(const Cookie& cookie);
    void add_cookie(const std::string& name, const std::string& value);
    const std::vector<Cookie>& cookies() const { return cookies_; }
    
    // Common response helpers
    void set_json(const std::string& json);
    void set_json(const char* json);
    template<typename T>
    void set_json(const T& obj) {
        (void)obj;
        // Would integrate with JSON library
        set_content_type("application/json");
    }
    
    void set_html(const std::string& html);
    void set_text(const std::string& text);
    void set_file(const std::string& file_path, const std::string& content_type = "");
    
    // Redirect
    void redirect(const std::string& url, HTTPStatus status = HTTPStatus::Found);
    
    // Enable compression
    void enable_compression(bool enable);
    bool compression_enabled() const { return compression_enabled_; }
    
    // Enable chunked encoding
    void enable_chunked_encoding(bool enable);
    bool chunked_encoding_enabled() const { return chunked_encoding_enabled_; }
    
    // Stream file transfer
    void enable_stream_file(const std::string& file_path);
    void set_stream_range(size_t start, size_t end);
    bool is_streaming_file() const { return is_streaming_file_; }
    const std::string& stream_file_path() const { return stream_file_path_; }
    int stream_file_fd() const { return stream_file_fd_; }
    size_t stream_start() const { return stream_start_; }
    size_t stream_end() const { return stream_end_; }
    
    // Serialize to buffer
    memory::ZeroCopyBuffer serialize() const;
    
    // Reset response
    void reset();
    
private:
    HTTPStatus status_;
    [[maybe_unused]] HTTPVersion version_;
    
    std::unordered_map<std::string, std::string> headers_;
    memory::ZeroCopyBuffer body_;
    mutable std::string body_string_;
    
    std::vector<Cookie> cookies_;
    
    bool compression_enabled_;
    bool chunked_encoding_enabled_;
    
    // 流式文件传输
    bool is_streaming_file_{false};
    std::string stream_file_path_;
    int stream_file_fd_{-1};
    size_t stream_start_{0};
    size_t stream_end_{SIZE_MAX};
};

// Response builder (for testing)
class ResponseBuilder {
public:
    ResponseBuilder();
    
    ResponseBuilder& status(HTTPStatus status);
    ResponseBuilder& header(const std::string& name, const std::string& value);
    ResponseBuilder& body(const std::string& body);
    ResponseBuilder& body(memory::ZeroCopyBuffer&& body);
    ResponseBuilder& content_type(const std::string& content_type);
    ResponseBuilder& json(const std::string& json);
    ResponseBuilder& html(const std::string& html);
    ResponseBuilder& redirect(const std::string& url, HTTPStatus status = HTTPStatus::Found);
    ResponseBuilder& cookie(const std::string& name, const std::string& value);
    ResponseBuilder& compression(bool enable);
    
    HTTPResponse build() const;
    
private:
    HTTPResponse response_;
};

// Static responses
class Responses {
public:
    static HTTPResponse ok(const std::string& body = "");
    static HTTPResponse created(const std::string& body = "");
    static HTTPResponse no_content();
    
    static HTTPResponse bad_request(const std::string& message = "");
    static HTTPResponse unauthorized(const std::string& message = "");
    static HTTPResponse forbidden(const std::string& message = "");
    static HTTPResponse not_found(const std::string& message = "");
    static HTTPResponse method_not_allowed(const std::string& message = "");
    
    static HTTPResponse internal_server_error(const std::string& message = "");
    static HTTPResponse service_unavailable(const std::string& message = "");
    
    static HTTPResponse json(const std::string& json);
    static HTTPResponse json(const char* json);
    template<typename T>
    static HTTPResponse json(const T& obj) {
        auto response = HTTPResponse(HTTPStatus::OK);
        response.set_json(obj);
        return response;
    }
    
    static HTTPResponse html(const std::string& html);
    static HTTPResponse text(const std::string& text);
    static HTTPResponse file(const std::string& file_path);
};

} // namespace network
} // namespace best_server

#endif // BEST_SERVER_NETWORK_HTTP_RESPONSE_HPP