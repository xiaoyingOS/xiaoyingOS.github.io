// HTTPResponse implementation
#include "best_server/network/http_response.hpp"
#include "best_server/network/http_request.hpp"
#include <sstream>
#include <algorithm>
#include <cctype>
#include <sys/stat.h>
#include <fstream>

namespace best_server {
namespace network {

// HTTP status message
std::string status_message(HTTPStatus status) {
    switch (status) {
        case HTTPStatus::Continue: return "Continue";
        case HTTPStatus::SwitchingProtocols: return "Switching Protocols";
        case HTTPStatus::OK: return "OK";
        case HTTPStatus::Created: return "Created";
        case HTTPStatus::Accepted: return "Accepted";
        case HTTPStatus::NoContent: return "No Content";
        case HTTPStatus::MovedPermanently: return "Moved Permanently";
        case HTTPStatus::Found: return "Found";
        case HTTPStatus::NotModified: return "Not Modified";
        case HTTPStatus::PartialContent: return "Partial Content";
        case HTTPStatus::BadRequest: return "Bad Request";
        case HTTPStatus::Unauthorized: return "Unauthorized";
        case HTTPStatus::Forbidden: return "Forbidden";
        case HTTPStatus::NotFound: return "Not Found";
        case HTTPStatus::MethodNotAllowed: return "Method Not Allowed";
        case HTTPStatus::RequestTimeout: return "Request Timeout";
        case HTTPStatus::Conflict: return "Conflict";
        case HTTPStatus::Gone: return "Gone";
        case HTTPStatus::PayloadTooLarge: return "Payload Too Large";
        case HTTPStatus::UnsupportedMediaType: return "Unsupported Media Type";
        case HTTPStatus::TooManyRequests: return "Too Many Requests";
        case HTTPStatus::InternalServerError: return "Internal Server Error";
        case HTTPStatus::NotImplemented: return "Not Implemented";
        case HTTPStatus::BadGateway: return "Bad Gateway";
        case HTTPStatus::ServiceUnavailable: return "Service Unavailable";
        case HTTPStatus::GatewayTimeout: return "Gateway Timeout";
        default: return "Unknown";
    }
}

// HTTPResponse implementation
HTTPResponse::HTTPResponse()
    : status_(HTTPStatus::OK)
    , version_(1, 1)
    , body_(64 * 1024)  // 64KB default buffer
    , compression_enabled_(false)
    , chunked_encoding_enabled_(false)
{
}

HTTPResponse::HTTPResponse(HTTPStatus status)
    : status_(status)
    , version_(1, 1)
    , body_(64 * 1024)
    , compression_enabled_(false)
    , chunked_encoding_enabled_(false)
{
}

HTTPResponse::~HTTPResponse() {
}

// Status
std::string HTTPResponse::status_message() const {
    return network::status_message(status_);
}

// Header operations - optimized with lowercase normalization on set
void HTTPResponse::set_header(const std::string& name, const std::string& value) {
    // Convert header name to lowercase for case-insensitive lookup (faster than canonical form)
    std::string lower_name;
    lower_name.reserve(name.size());
    
    for (char c : name) {
        if (c >= 'A' && c <= 'Z') {
            lower_name += static_cast<char>(c + 32);  // Faster than tolower
        } else {
            lower_name += c;
        }
    }
    
    headers_[lower_name] = value;
}

std::string HTTPResponse::get_header(const std::string& name, const std::string& default_value) const {
    // Convert lookup name to lowercase
    std::string lower_name;
    lower_name.reserve(name.size());
    
    for (char c : name) {
        if (c >= 'A' && c <= 'Z') {
            lower_name += static_cast<char>(c + 32);
        } else {
            lower_name += c;
        }
    }
    
    auto it = headers_.find(lower_name);
    if (it != headers_.end()) {
        return it->second;
    }
    
    return default_value;
}

bool HTTPResponse::has_header(const std::string& name) const {
    return !get_header(name).empty();
}

void HTTPResponse::remove_header(const std::string& name) {
    auto it = headers_.find(name);
    if (it != headers_.end()) {
        headers_.erase(it);
    }
}

// Body operations
void HTTPResponse::set_body(memory::ZeroCopyBuffer&& body) {
    body_ = std::move(body);
    body_string_.clear();
}

void HTTPResponse::set_body(const std::string& body) {
    body_.clear();
    body_.write(body.data(), body.size());
    body_string_.clear();
}

const std::string& HTTPResponse::body_string() const {
    if (body_string_.empty() && body_.size() > 0) {
        body_string_.assign(body_.data(), body_.size());
    }
    return body_string_;
}

// Content type
void HTTPResponse::set_content_type(const std::string& content_type) {
    set_header("Content-Type", content_type);
}

// Content length
size_t HTTPResponse::content_length() const {
    if (chunked_encoding_enabled_) {
        return 0;  // Content-Length is not used with chunked encoding
    }
    return body_.size();
}

// Cookie operations
void HTTPResponse::add_cookie(const Cookie& cookie) {
    cookies_.push_back(cookie);
}

void HTTPResponse::add_cookie(const std::string& name, const std::string& value) {
    Cookie cookie;
    cookie.name = name;
    cookie.value = value;
    cookies_.push_back(cookie);
}

// JSON response
void HTTPResponse::set_json(const std::string& json) {
    set_content_type("application/json");
    set_body(json);
}

void HTTPResponse::set_json(const char* json) {
    set_json(std::string(json));
}

// HTML response
void HTTPResponse::set_html(const std::string& html) {
    set_content_type("text/html; charset=utf-8");
    set_body(html);
}

// Text response
void HTTPResponse::set_text(const std::string& text) {
    set_content_type("text/plain; charset=utf-8");
    set_body(text);
}

// File response
void HTTPResponse::set_file(const std::string& file_path, const std::string& content_type) {
    // Read file
    std::ifstream file(file_path, std::ios::binary);
    if (!file.is_open()) {
        set_status(HTTPStatus::NotFound);
        set_body("File not found");
        return;
    }
    
    // Get file size
    file.seekg(0, std::ios::end);
    size_t file_size = file.tellg();
    file.seekg(0, std::ios::beg);
    
    // For large files (>10MB), use chunked encoding
    constexpr size_t LARGE_FILE_THRESHOLD = 10 * 1024 * 1024; // 10MB
    
    if (file_size > LARGE_FILE_THRESHOLD) {
        // Enable chunked encoding for large files
        enable_chunked_encoding(true);
        
        // Read file in chunks (1MB at a time)
        constexpr size_t CHUNK_SIZE = 1 * 1024 * 1024; // 1MB
        std::vector<char> chunk_buffer(CHUNK_SIZE);
        size_t bytes_remaining = file_size;
        
        // Read all chunks and accumulate them
        memory::ZeroCopyBuffer buffer(0);
        
        while (bytes_remaining > 0) {
            size_t bytes_to_read = std::min(CHUNK_SIZE, bytes_remaining);
            file.read(chunk_buffer.data(), bytes_to_read);
            
            if (file.gcount() > 0) {
                buffer.write(chunk_buffer.data(), file.gcount());
                bytes_remaining -= file.gcount();
            } else {
                break; // Error or EOF
            }
        }
        
        file.close();
        
        set_body(std::move(buffer));
    } else {
        // For small files, read entire file at once
        memory::ZeroCopyBuffer buffer(file_size);
        char* data = buffer.data();
        file.read(data, file_size);
        file.close();
        
        set_body(std::move(buffer));
    }
    
    // Set content type
    if (!content_type.empty()) {
        set_content_type(content_type);
    } else {
        // Detect content type from file extension
        size_t dot_pos = file_path.find_last_of('.');
        if (dot_pos != std::string::npos) {
            std::string ext = file_path.substr(dot_pos + 1);
            std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
            
            if (ext == "html" || ext == "htm") {
                set_content_type("text/html; charset=utf-8");
            } else if (ext == "css") {
                set_content_type("text/css; charset=utf-8");
            } else if (ext == "js") {
                set_content_type("application/javascript; charset=utf-8");
            } else if (ext == "json") {
                set_content_type("application/json; charset=utf-8");
            } else if (ext == "xml") {
                set_content_type("application/xml; charset=utf-8");
            } else if (ext == "png") {
                set_content_type("image/png");
            } else if (ext == "jpg" || ext == "jpeg") {
                set_content_type("image/jpeg");
            } else if (ext == "gif") {
                set_content_type("image/gif");
            } else if (ext == "svg") {
                set_content_type("image/svg+xml");
            } else if (ext == "pdf") {
                set_content_type("application/pdf");
            } else if (ext == "zip") {
                set_content_type("application/zip");
            } else {
                set_content_type("application/octet-stream");
            }
        } else {
            set_content_type("application/octet-stream");
        }
    }
}

// Redirect
void HTTPResponse::redirect(const std::string& url, HTTPStatus status) {
    set_status(status);
    set_header("Location", url);
    if (status == HTTPStatus::MovedPermanently || status == HTTPStatus::Found) {
        std::string body = "<html><body><h1>" + std::to_string(static_cast<int>(status)) + " " + 
                          ::best_server::network::status_message(status) + "</h1><p>Redirecting to <a href=\"" + url + "\">" + 
                          url + "</a></p></body></html>";
        set_html(body);
    }
}

// Compression
void HTTPResponse::enable_compression(bool enable) {
    compression_enabled_ = enable;
    if (enable) {
        set_header("Content-Encoding", "gzip");
    } else {
        remove_header("Content-Encoding");
    }
}

// Chunked encoding
void HTTPResponse::enable_chunked_encoding(bool enable) {
    chunked_encoding_enabled_ = enable;
    if (enable) {
        set_header("Transfer-Encoding", "chunked");
        remove_header("Content-Length");
    } else {
        remove_header("Transfer-Encoding");
        set_header("Content-Length", std::to_string(body_.size()));
    }
}

// Serialize to buffer
memory::ZeroCopyBuffer HTTPResponse::serialize() const {
    // Calculate required size
    size_t size = 0;
    
    // Status line: HTTP/1.1 200 OK\r\n
    size += 12 + 3 + status_message().size() + 2;
    
// Write headers
    bool has_content_length = false;
    bool has_transfer_encoding = false;
    for (const auto& [name, value] : headers_) {
        size += name.size() + 2 + value.size() + 2;
        if (name == "Content-Length") {
            has_content_length = true;
        } else if (name == "Transfer-Encoding") {
            has_transfer_encoding = true;
        }
    }
    
    // Auto-add Content-Length size if body has content and no Transfer-Encoding
    if (!has_transfer_encoding && !has_content_length && body_.size() > 0) {
        size += 16 + std::to_string(body_.size()).size() + 2;  // "Content-Length: " + size + "\r\n"
    }
    
    // Cookies
    for (const auto& cookie : cookies_) {
        size += 8;  // "Set-Cookie: "
        size += cookie.name.size() + 1 + cookie.value.size();
        if (!cookie.path.empty()) {
            size += 7 + cookie.path.size();  // "; Path="
        }
        if (!cookie.domain.empty()) {
            size += 9 + cookie.domain.size();  // "; Domain="
        }
        if (cookie.max_age > 0) {
            size += 10 + std::to_string(cookie.max_age).size();  // "; Max-Age="
        }
        if (cookie.secure) {
            size += 9;  // "; Secure"
        }
        if (cookie.http_only) {
            size += 11;  // "; HttpOnly"
        }
        if (cookie.same_site) {
            size += 11;  // "; SameSite"
        }
        size += 2;  // "\r\n"
    }
    
    // Empty line
    size += 2;
    
    // Body
    if (!chunked_encoding_enabled_ && body_.size() > 0) {
        size += body_.size();
    }
    
    // Create buffer
    memory::ZeroCopyBuffer buffer(size);
    
    // Write status line
    std::string status_line = "HTTP/" + std::to_string(version_.major) + "." + 
                              std::to_string(version_.minor) + " " + 
                              std::to_string(static_cast<int>(status_)) + " " + 
                              status_message() + "\r\n";
    buffer.write(status_line.data(), status_line.size());
    
    // Write headers
    for (const auto& [name, value] : headers_) {
        std::string header = name + ": " + value + "\r\n";
        buffer.write(header.data(), header.size());
    }
    
    // Auto-add Content-Length if body has content and no Transfer-Encoding
    if (!has_transfer_encoding && !has_content_length && body_.size() > 0) {
        std::string content_length_header = "Content-Length: " + std::to_string(body_.size()) + "\r\n";
        buffer.write(content_length_header.data(), content_length_header.size());
    }
    
    // Write cookies
    for (const auto& cookie : cookies_) {
        std::string cookie_str = "Set-Cookie: " + cookie.name + "=" + cookie.value;
        
        if (!cookie.path.empty()) {
            cookie_str += "; Path=" + cookie.path;
        }
        if (!cookie.domain.empty()) {
            cookie_str += "; Domain=" + cookie.domain;
        }
        if (cookie.max_age > 0) {
            cookie_str += "; Max-Age=" + std::to_string(cookie.max_age);
        }
        if (cookie.secure) {
            cookie_str += "; Secure";
        }
        if (cookie.http_only) {
            cookie_str += "; HttpOnly";
        }
        if (cookie.same_site) {
            cookie_str += "; SameSite=Strict";
        }
        
        cookie_str += "\r\n";
        buffer.write(cookie_str.data(), cookie_str.size());
    }
    
    // Empty line
    buffer.write("\r\n", 2);
    
    // Write body
    if (!chunked_encoding_enabled_ && body_.size() > 0) {
        buffer.write(body_.data(), body_.size());
    }
    
    return buffer;
}

// Reset response
void HTTPResponse::reset() {
    status_ = HTTPStatus::OK;
    version_ = HTTPVersion(1, 1);
    headers_.clear();
    body_.clear();
    body_string_.clear();
    cookies_.clear();
    compression_enabled_ = false;
    chunked_encoding_enabled_ = false;
}

// ResponseBuilder implementation
ResponseBuilder::ResponseBuilder() {
}

ResponseBuilder& ResponseBuilder::status(HTTPStatus status) {
    response_.set_status(status);
    return *this;
}

ResponseBuilder& ResponseBuilder::header(const std::string& name, const std::string& value) {
    response_.set_header(name, value);
    return *this;
}

ResponseBuilder& ResponseBuilder::body(const std::string& body) {
    response_.set_body(body);
    return *this;
}

ResponseBuilder& ResponseBuilder::body(memory::ZeroCopyBuffer&& body) {
    response_.set_body(std::move(body));
    return *this;
}

ResponseBuilder& ResponseBuilder::content_type(const std::string& content_type) {
    response_.set_content_type(content_type);
    return *this;
}

ResponseBuilder& ResponseBuilder::json(const std::string& json) {
    response_.set_json(json);
    return *this;
}

ResponseBuilder& ResponseBuilder::html(const std::string& html) {
    response_.set_html(html);
    return *this;
}

ResponseBuilder& ResponseBuilder::redirect(const std::string& url, HTTPStatus status) {
    response_.redirect(url, status);
    return *this;
}

ResponseBuilder& ResponseBuilder::cookie(const std::string& name, const std::string& value) {
    response_.add_cookie(name, value);
    return *this;
}

ResponseBuilder& ResponseBuilder::compression(bool enable) {
    response_.enable_compression(enable);
    return *this;
}

HTTPResponse ResponseBuilder::build() const {
    return response_;
}

// Static responses
HTTPResponse Responses::ok(const std::string& body) {
    HTTPResponse response(HTTPStatus::OK);
    if (!body.empty()) {
        response.set_text(body);
    }
    return response;
}

HTTPResponse Responses::created(const std::string& body) {
    HTTPResponse response(HTTPStatus::Created);
    if (!body.empty()) {
        response.set_text(body);
    }
    return response;
}

HTTPResponse Responses::no_content() {
    return HTTPResponse(HTTPStatus::NoContent);
}

HTTPResponse Responses::bad_request(const std::string& message) {
    HTTPResponse response(HTTPStatus::BadRequest);
    response.set_text(message.empty() ? "Bad Request" : message);
    return response;
}

HTTPResponse Responses::unauthorized(const std::string& message) {
    HTTPResponse response(HTTPStatus::Unauthorized);
    response.set_text(message.empty() ? "Unauthorized" : message);
    return response;
}

HTTPResponse Responses::forbidden(const std::string& message) {
    HTTPResponse response(HTTPStatus::Forbidden);
    response.set_text(message.empty() ? "Forbidden" : message);
    return response;
}

HTTPResponse Responses::not_found(const std::string& message) {
    HTTPResponse response(HTTPStatus::NotFound);
    response.set_text(message.empty() ? "Not Found" : message);
    return response;
}

HTTPResponse Responses::method_not_allowed(const std::string& message) {
    HTTPResponse response(HTTPStatus::MethodNotAllowed);
    response.set_text(message.empty() ? "Method Not Allowed" : message);
    return response;
}

HTTPResponse Responses::internal_server_error(const std::string& message) {
    HTTPResponse response(HTTPStatus::InternalServerError);
    response.set_text(message.empty() ? "Internal Server Error" : message);
    return response;
}

HTTPResponse Responses::service_unavailable(const std::string& message) {
    HTTPResponse response(HTTPStatus::ServiceUnavailable);
    response.set_text(message.empty() ? "Service Unavailable" : message);
    return response;
}

HTTPResponse Responses::json(const std::string& json_data) {
    HTTPResponse response(HTTPStatus::OK);
    response.set_json(json_data);
    return response;
}

HTTPResponse Responses::json(const char* json_data) {
    return json(std::string(json_data));
}

HTTPResponse Responses::html(const std::string& html) {
    HTTPResponse response(HTTPStatus::OK);
    response.set_html(html);
    return response;
}

HTTPResponse Responses::text(const std::string& text) {
    HTTPResponse response(HTTPStatus::OK);
    response.set_text(text);
    return response;
}

HTTPResponse Responses::file(const std::string& file_path) {
    HTTPResponse response(HTTPStatus::OK);
    response.set_file(file_path);
    return response;
}

// 流式文件传输
void HTTPResponse::enable_stream_file(const std::string& file_path) {
    is_streaming_file_ = true;
    stream_file_path_ = file_path;
    stream_file_fd_ = -1;
    stream_start_ = 0;
    stream_end_ = SIZE_MAX;
}

void HTTPResponse::set_stream_range(size_t start, size_t end) {
    stream_start_ = start;
    stream_end_ = end;
}

} // namespace network
} // namespace best_server