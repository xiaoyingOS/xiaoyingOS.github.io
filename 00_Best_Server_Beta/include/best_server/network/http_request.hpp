// HTTPRequest - HTTP request representation
// 
// Represents an HTTP request with:
// - Method, URL, version
// - Headers
// - Body
// - Query parameters
// - Cookies
// - Multipart form data

#ifndef BEST_SERVER_NETWORK_HTTP_REQUEST_HPP
#define BEST_SERVER_NETWORK_HTTP_REQUEST_HPP

#include <string>
#include <unordered_map>
#include <vector>
#include <memory>

#include "memory/zero_copy_buffer.hpp"

namespace best_server {
namespace network {

// HTTP method
enum class HTTPMethod {
    GET,
    POST,
    PUT,
    DELETE,
    PATCH,
    HEAD,
    OPTIONS,
    TRACE,
    CONNECT
};

// Convert method to string
std::string method_to_string(HTTPMethod method);
HTTPMethod string_to_method(const std::string& str);

// HTTP version
struct HTTPVersion {
    uint8_t major;
    uint8_t minor;
    
    HTTPVersion(uint8_t maj = 1, uint8_t min = 1) : major(maj), minor(min) {}
    
    std::string to_string() const;
};

// HTTP request
class HTTPRequest {
public:
    HTTPRequest();
    ~HTTPRequest();
    
    // Method
    HTTPMethod method() const { return method_; }
    void set_method(HTTPMethod method) { method_ = method; }
    
    // URL
    const std::string& url() const { return url_; }
    void set_url(const std::string& url) { url_ = url; }
    
    // Path
    const std::string& path() const { return path_; }
    void set_path(const std::string& path) { path_ = path; }
    
    // Query string
    const std::string& query_string() const { return query_string_; }
    void set_query_string(const std::string& query) { query_string_ = query; }
    
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
    const std::string& body_string() const;
    
    // Query parameters
    const std::unordered_map<std::string, std::string>& query_params() const { return query_params_; }
    std::string get_query_param(const std::string& name, const std::string& default_value = "") const;
    bool has_query_param(const std::string& name) const;
    
    // Cookies
    const std::unordered_map<std::string, std::string>& cookies() const { return cookies_; }
    std::string get_cookie(const std::string& name, const std::string& default_value = "") const;
    bool has_cookie(const std::string& name) const;
    
    // Remote address
    const std::string& remote_address() const { return remote_address_; }
    void set_remote_address(const std::string& address) { remote_address_ = address; }
    
    // Content type
    std::string content_type() const { return get_header("Content-Type", ""); }
    
    // Content length
    size_t content_length() const;
    
    // User agent
    std::string user_agent() const { return get_header("User-Agent", ""); }
    
    // Accept
    std::string accept() const { return get_header("Accept", ""); }
    
    // Authorization
    std::string authorization() const { return get_header("Authorization", ""); }
    
    // Keep-alive
    bool keep_alive() const;
    
    // Range header (for partial content)
    struct Range {
        size_t start;
        size_t end;
        size_t size;
        bool valid;
    };
    Range get_range() const;
    
    // Parse query string
    void parse_query_string();
    
    // Parse cookies
    void parse_cookies();
    
    // Parse multipart form data
    struct FormField {
        std::string name;
        std::string filename;
        std::string content_type;
        memory::ZeroCopyBuffer data;
    };
    std::vector<FormField> parse_multipart_form_data() const;
    
    // Parse JSON body
    template<typename T>
    T parse_json() const {
        // Would integrate with JSON library
        return T{};
    }
    
    // Reset request
    void reset();
    
private:
    HTTPMethod method_;
    std::string url_;
    std::string path_;
    std::string query_string_;
    HTTPVersion version_;
    
    std::unordered_map<std::string, std::string> headers_;
    memory::ZeroCopyBuffer body_;
    mutable std::string body_string_;
    
    std::unordered_map<std::string, std::string> query_params_;
    std::unordered_map<std::string, std::string> cookies_;
    
    std::string remote_address_;
};

// Request builder (for testing)
class RequestBuilder {
public:
    RequestBuilder();
    
    RequestBuilder& method(HTTPMethod method);
    RequestBuilder& url(const std::string& url);
    RequestBuilder& header(const std::string& name, const std::string& value);
    RequestBuilder& body(const std::string& body);
    RequestBuilder& body(memory::ZeroCopyBuffer&& body);
    RequestBuilder& query_param(const std::string& name, const std::string& value);
    RequestBuilder& cookie(const std::string& name, const std::string& value);
    
    HTTPRequest build() const;
    
private:
    HTTPRequest request_;
};

} // namespace network
} // namespace best_server

#endif // BEST_SERVER_NETWORK_HTTP_REQUEST_HPP