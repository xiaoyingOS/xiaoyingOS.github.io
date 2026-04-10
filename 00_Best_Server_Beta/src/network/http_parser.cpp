// HTTPParser implementation
#include "best_server/network/http_parser.hpp"
#include "best_server/network/simd_utils.hpp"
#include <cstring>
#include <cctype>
#include <algorithm>

namespace best_server {
namespace network {

// HTTPParser implementation
HTTPParser::HTTPParser()
    : state_(ParserState::Start)
    , request_(std::make_unique<HTTPRequest>())
    , response_(std::make_unique<HTTPResponse>())
    , bytes_consumed_(0)
    , content_length_(0)
    , body_bytes_read_(0)
    , is_chunked_(false)
    , current_chunk_size_(0)
    , current_chunk_bytes_read_(0)
{
}

HTTPParser::~HTTPParser() {
}

// Set event callbacks
void HTTPParser::set_events(ParserEvents events) {
    events_ = events;
}

// Parse HTTP request
bool HTTPParser::parse_request(const char* data, size_t size) {
    
    size_t pos = 0;
    /* size_t iteration = 0;  */  // DEBUG: Removed unused variable
    
    while (pos < size && state_ != ParserState::Complete && state_ != ParserState::Error) {
        
        switch (state_) {
            case ParserState::Start:
                if (!parse_request_line(data, size, pos)) {
                    return false;
                }
                break;
                
            case ParserState::HeaderName:
            case ParserState::HeaderValue:
                if (!parse_headers(data, size, pos)) {
                    return false;
                }
                break;
                
            case ParserState::Body:
                if (!parse_body(data, size, pos)) {
                    return false;
                }
                break;
                
            case ParserState::ChunkSize:
            case ParserState::ChunkData:
            case ParserState::ChunkEnd:
                if (!parse_chunk(data, size, pos)) {
                    return false;
                }
                break;
                
            default:
                break;
        }
    }
    
    bytes_consumed_ = pos;
    
    if (state_ == ParserState::Complete && events_.on_complete) {
        events_.on_complete();
    }
    
    return state_ != ParserState::Error;
}

bool HTTPParser::parse_request(const memory::ZeroCopyBuffer& buffer) {
    return parse_request(buffer.data(), buffer.size());
}

// Parse HTTP response
bool HTTPParser::parse_response(const char* data, size_t size) {
    size_t pos = 0;
    
    while (pos < size && state_ != ParserState::Complete && state_ != ParserState::Error) {
        switch (state_) {
            case ParserState::Start:
                if (!parse_status_line(data, size, pos)) {
                    return false;
                }
                break;
                
            case ParserState::HeaderName:
            case ParserState::HeaderValue:
                if (!parse_headers(data, size, pos)) {
                    return false;
                }
                break;
                
            case ParserState::Body:
                if (!parse_body(data, size, pos)) {
                    return false;
                }
                break;
                
            case ParserState::ChunkSize:
            case ParserState::ChunkData:
            case ParserState::ChunkEnd:
                if (!parse_chunk(data, size, pos)) {
                    return false;
                }
                break;
                
            default:
                break;
        }
    }
    
    bytes_consumed_ = pos;
    
    if (state_ == ParserState::Complete && events_.on_complete) {
        events_.on_complete();
    }
    
    return state_ != ParserState::Error;
}

bool HTTPParser::parse_response(const memory::ZeroCopyBuffer& buffer) {
    return parse_response(buffer.data(), buffer.size());
}

// Parse request line
bool HTTPParser::parse_request_line(const char* data, size_t size, size_t& pos) {
    
    // Find end of line using SIMD optimization
    const char* cr_pos = SIMDUtils::find_crlf_simd(data + pos, size - pos);
    size_t line_end = (cr_pos - (data + pos)) + pos;  // Fix: cr_pos is relative to data+pos
    
    
    
    bool cond1 = (line_end >= size);
    bool cond2 = (line_end + 1 >= size);
    
    if (cond1 || cond2) {
        return true;  // Incomplete line
    }
    
    
    if (data[line_end + 1] != '\n') {
        return true;  // Incomplete line
    }
    
    
    // Parse method
    size_t method_start = pos;
    size_t method_end = method_start;
    while (method_end < line_end && !::isspace(data[method_end])) {
        method_end++;
    }
    
    if (method_end >= line_end) {
        error_message_ = "Invalid request line: missing method";
        state_ = ParserState::Error;
        return false;
    }
    
    std::string method_str(data + method_start, method_end - method_start);
    HTTPMethod method = string_to_method(method_str);
    request_->set_method(method);
    
    // Skip whitespace
    pos = method_end;
    while (pos < line_end && ::isspace(data[pos])) {
        pos++;
    }
    
    // Parse URL
    size_t url_start = pos;
    size_t url_end = url_start;
    while (url_end < line_end && !::isspace(data[url_end])) {
        url_end++;
    }
    
    if (url_end >= line_end) {
        error_message_ = "Invalid request line: missing URL";
        state_ = ParserState::Error;
        return false;
    }
    
    std::string url(data + url_start, url_end - url_start);
    request_->set_url(url);
    
    // Parse query string
    size_t query_pos = url.find('?');
    if (query_pos != std::string::npos) {
        request_->set_path(url.substr(0, query_pos));
        request_->set_query_string(url.substr(query_pos + 1));
    } else {
        request_->set_path(url);
    }
    
    // Skip whitespace
    pos = url_end;
    while (pos < line_end && ::isspace(data[pos])) {
        pos++;
    }
    
    // Parse version
    size_t version_start = pos;
    if (version_start + 5 >= line_end || 
        strncmp(data + version_start, "HTTP/", 5) != 0) {
        error_message_ = "Invalid request line: missing HTTP version";
        state_ = ParserState::Error;
        return false;
    }
    
    version_start += 5;
    size_t dot_pos = version_start;
    while (dot_pos < line_end && data[dot_pos] != '.') {
        dot_pos++;
    }
    
    if (dot_pos >= line_end) {
        error_message_ = "Invalid request line: malformed HTTP version";
        state_ = ParserState::Error;
        return false;
    }
    
    uint8_t major = std::stoi(std::string(data + version_start, dot_pos - version_start));
    uint8_t minor = std::stoi(std::string(data + dot_pos + 1, line_end - dot_pos - 1));
    
    HTTPVersion version(major, minor);
    request_->set_version(version);
    
    // Move past CRLF
    pos = line_end + 2;
    state_ = ParserState::HeaderName;
    
    if (events_.on_request_line) {
        events_.on_request_line(method, url, version);
    }
    
    return true;
}

// Parse status line
bool HTTPParser::parse_status_line(const char* data, size_t size, size_t& pos) {
    // Find end of line using SIMD optimization
    const char* cr_pos = SIMDUtils::find_crlf_simd(data + pos, size - pos);
    size_t line_end = cr_pos - data;
    
    if (line_end >= size || line_end + 1 >= size || data[line_end + 1] != '\n') {
        return true;  // Incomplete line
    }
    
    // Parse HTTP version
    if (pos + 5 >= line_end || strncmp(data + pos, "HTTP/", 5) != 0) {
        error_message_ = "Invalid status line: missing HTTP version";
        state_ = ParserState::Error;
        return false;
    }
    
    pos += 5;
    size_t dot_pos = pos;
    while (dot_pos < line_end && data[dot_pos] != '.') {
        dot_pos++;
    }
    
    if (dot_pos >= line_end) {
        error_message_ = "Invalid status line: malformed HTTP version";
        state_ = ParserState::Error;
        return false;
    }
    
    uint8_t major = std::stoi(std::string(data + pos, dot_pos - pos));
    uint8_t minor = std::stoi(std::string(data + dot_pos + 1, line_end - dot_pos - 1));
    
    HTTPVersion version(major, minor);
    response_->set_version(version);
    
    // Skip whitespace
    pos = line_end + 1;
    while (pos < line_end && ::isspace(data[pos])) {
        pos++;
    }
    
    // Parse status code
    size_t code_start = pos;
    size_t code_end = code_start;
    while (code_end < line_end && ::isdigit(data[code_end])) {
        code_end++;
    }
    
    if (code_end >= line_end || code_end - code_start != 3) {
        error_message_ = "Invalid status line: missing status code";
        state_ = ParserState::Error;
        return false;
    }
    
    int status_code = std::stoi(std::string(data + code_start, code_end - code_start));
    response_->set_status(static_cast<HTTPStatus>(status_code));
    
    // Move past CRLF
    pos = line_end + 2;
    state_ = ParserState::HeaderName;
    
    return true;
}

// Parse headers
bool HTTPParser::parse_headers(const char* data, size_t size, size_t& pos) {
    while (pos < size) {
        // Check for end of headers
        if (pos + 1 < size && data[pos] == '\r' && data[pos + 1] == '\n') {
            pos += 2;
            
            // Check for content length
            if (request_) {
                content_length_ = request_->content_length();
            } else if (response_) {
                content_length_ = response_->content_length();
            }
            
            // Check for chunked encoding
            std::string transfer_encoding;
            if (request_) {
                transfer_encoding = request_->get_header("Transfer-Encoding");
            } else if (response_) {
                transfer_encoding = response_->get_header("Transfer-Encoding");
            }
            
            is_chunked_ = (transfer_encoding.find("chunked") != std::string::npos);
            
            if (is_chunked_) {
                state_ = ParserState::ChunkSize;
            } else if (content_length_ > 0) {
                state_ = ParserState::Body;
            } else {
                state_ = ParserState::Complete;
            }
            
            return true;
        }
        
        // Parse header name using SIMD optimization
        size_t name_start = pos;
        const char* colon_pos = SIMDUtils::find_char_simd(data + pos, size - pos, ':');
        if (colon_pos >= data + size) {
            return true;  // Incomplete header
        }
        pos = colon_pos - data;
        
        if (pos >= size) {
            return true;  // Incomplete header
        }
        
        std::string name(data + name_start, pos - name_start);
        
        // Skip colon and whitespace
        pos++;
        while (pos < size && ::isspace(data[pos])) {
            pos++;
        }
        
        // Parse header value
        size_t value_start = pos;
        while (pos < size && data[pos] != '\r') {
            pos++;
        }
        
        if (pos >= size) {
            return true;  // Incomplete header value
        }
        
        std::string value(data + value_start, pos - value_start);
        
        // Trim trailing whitespace
        while (!value.empty() && ::isspace(value.back())) {
            value.pop_back();
        }
        
        // Set header
        if (request_) {
            request_->set_header(name, value);
        } else if (response_) {
            response_->set_header(name, value);
        }
        
        if (events_.on_header) {
            events_.on_header(name, value);
        }
        
        // Skip CRLF
        pos += 2;
    }
    
    return true;
}

// Parse body
bool HTTPParser::parse_body(const char* data, size_t size, size_t& pos) {
    if (body_bytes_read_ >= content_length_) {
        state_ = ParserState::Complete;
        return true;
    }
    
    size_t bytes_to_read = std::min(size - pos, content_length_ - body_bytes_read_);
    
    if (bytes_to_read > 0) {
        if (request_) {
            request_->body().write(data + pos, bytes_to_read);
        } else if (response_) {
            response_->body().write(data + pos, bytes_to_read);
        }
        
        if (events_.on_body) {
            events_.on_body(data + pos, bytes_to_read);
        }
        
        body_bytes_read_ += bytes_to_read;
        pos += bytes_to_read;
    }
    
    if (body_bytes_read_ >= content_length_) {
        state_ = ParserState::Complete;
    }
    
    return true;
}

// Parse chunk
bool HTTPParser::parse_chunk(const char* data, size_t size, size_t& pos) {
    while (pos < size) {
        switch (state_) {
            case ParserState::ChunkSize: {
                // Find end of line
                size_t line_end = pos;
                while (line_end < size && data[line_end] != '\r') {
                    line_end++;
                }
                
                if (line_end >= size || line_end + 1 >= size || data[line_end + 1] != '\n') {
                    return true;  // Incomplete line
                }
                
                // Parse chunk size
                std::string size_str(data + pos, line_end - pos);
                current_chunk_size_ = std::stoul(size_str, nullptr, 16);
                
                if (current_chunk_size_ == 0) {
                    state_ = ParserState::Complete;
                } else {
                    state_ = ParserState::ChunkData;
                    current_chunk_bytes_read_ = 0;
                }
                
                pos = line_end + 2;
                break;
            }
            
            case ParserState::ChunkData: {
                size_t bytes_to_read = std::min(size - pos, current_chunk_size_ - current_chunk_bytes_read_);
                
                if (bytes_to_read > 0) {
                    if (request_) {
                        request_->body().write(data + pos, bytes_to_read);
                    } else if (response_) {
                        response_->body().write(data + pos, bytes_to_read);
                    }
                    
                    if (events_.on_body) {
                        events_.on_body(data + pos, bytes_to_read);
                    }
                    
                    current_chunk_bytes_read_ += bytes_to_read;
                    pos += bytes_to_read;
                }
                
                if (current_chunk_bytes_read_ >= current_chunk_size_) {
                    state_ = ParserState::ChunkEnd;
                }
                break;
            }
            
            case ParserState::ChunkEnd: {
                // Skip CRLF
                if (pos + 1 < size && data[pos] == '\r' && data[pos + 1] == '\n') {
                    pos += 2;
                    state_ = ParserState::ChunkSize;
                } else if (pos < size) {
                    pos++;  // Skip one character
                }
                break;
            }
            
            default:
                break;
        }
    }
    
    return true;
}

// Utility methods
bool HTTPParser::skip_whitespace(const char* data, size_t size, size_t& pos) {
    while (pos < size && ::isspace(data[pos])) {
        pos++;
    }
    return true;
}

bool HTTPParser::read_token(const char* data, size_t size, size_t& pos, std::string& token, char delimiter) {
    size_t start = pos;
    while (pos < size && data[pos] != delimiter) {
        pos++;
    }
    
    if (pos >= size) {
        return false;
    }
    
    token = std::string(data + start, pos - start);
    pos++;  // Skip delimiter
    return true;
}

bool HTTPParser::read_line(const char* data, size_t size, size_t& pos, std::string& line) {
    size_t start = pos;
    while (pos < size && data[pos] != '\r') {
        pos++;
    }
    
    if (pos >= size || pos + 1 >= size || data[pos + 1] != '\n') {
        return false;
    }
    
    line = std::string(data + start, pos - start);
    pos += 2;  // Skip CRLF
    return true;
}

bool HTTPParser::read_until(const char* data, size_t size, size_t& pos, std::string& str, char delimiter) {
    size_t start = pos;
    while (pos < size && data[pos] != delimiter) {
        pos++;
    }
    
    if (pos >= size) {
        return false;
    }
    
    str = std::string(data + start, pos - start);
    return true;
}

// Reset parser
void HTTPParser::reset() {
    state_ = ParserState::Start;
    request_ = std::make_unique<HTTPRequest>();
    response_ = std::make_unique<HTTPResponse>();
    error_message_.clear();
    bytes_consumed_ = 0;
    content_length_ = 0;
    body_bytes_read_ = 0;
    is_chunked_ = false;
    current_chunk_size_ = 0;
    current_chunk_bytes_read_ = 0;
    temp_buffer_.clear();
    current_header_name_.clear();
    current_header_value_.clear();
}

// HTTPRequestSerializer implementation
HTTPRequestSerializer::HTTPRequestSerializer() {
}

HTTPRequestSerializer::~HTTPRequestSerializer() {
}

size_t HTTPRequestSerializer::required_size(const HTTPRequest& request) const {
    size_t size = 0;
    
    // Request line: GET /path HTTP/1.1\r\n
    size += method_to_string(request.method()).size() + 1 + request.url().size() + 1 + 8 + 2;
    
    // Headers
    for (const auto& [name, value] : request.headers()) {
        size += name.size() + 2 + value.size() + 2;
    }
    
    // Empty line
    size += 2;
    
    // Body
    size += request.body().size();
    
    return size;
}

memory::ZeroCopyBuffer HTTPRequestSerializer::serialize(const HTTPRequest& request) {
    size_t size = required_size(request);
    memory::ZeroCopyBuffer buffer(size);
    
    char* ptr = static_cast<char*>(buffer.data());
    [[maybe_unused]] size_t remaining = size;  // For future buffer overflow checking
    
    // Write request line directly to buffer
    const std::string& method = method_to_string(request.method());
    const std::string& url = request.url();
    const auto& version = request.version();
    
    // Method
    std::memcpy(ptr, method.data(), method.size());
    ptr += method.size();
    
    // Space
    *ptr++ = ' ';
    
    // URL
    std::memcpy(ptr, url.data(), url.size());
    ptr += url.size();
    
    // Space
    *ptr++ = ' ';
    
    // HTTP version
    const char* http_version = "HTTP/";
    std::memcpy(ptr, http_version, 5);
    ptr += 5;
    
    // Major version
    char major_buf[16];
    int major_len = snprintf(major_buf, sizeof(major_buf), "%d", version.major);
    std::memcpy(ptr, major_buf, major_len);
    ptr += major_len;
    
    // Dot
    *ptr++ = '.';
    
    // Minor version
    char minor_buf[16];
    int minor_len = snprintf(minor_buf, sizeof(minor_buf), "%d", version.minor);
    std::memcpy(ptr, minor_buf, minor_len);
    ptr += minor_len;
    
    // CRLF
    *ptr++ = '\r';
    *ptr++ = '\n';
    
    // Write headers
    for (const auto& [name, value] : request.headers()) {
        const std::string& header_name = name;
        const std::string& header_value = value;
        
        std::memcpy(ptr, header_name.data(), header_name.size());
        ptr += header_name.size();
        
        *ptr++ = ':';
        *ptr++ = ' ';
        
        std::memcpy(ptr, header_value.data(), header_value.size());
        ptr += header_value.size();
        
        *ptr++ = '\r';
        *ptr++ = '\n';
    }
    
    // Empty line
    *ptr++ = '\r';
    *ptr++ = '\n';
    remaining -= 2;
    
    // Write body
    if (request.body().size() > 0) {
        std::memcpy(ptr, request.body().data(), request.body().size());
    }
    
    return buffer;
}

// HTTPResponseSerializer implementation
HTTPResponseSerializer::HTTPResponseSerializer() {
}

HTTPResponseSerializer::~HTTPResponseSerializer() {
}

size_t HTTPResponseSerializer::required_size(const HTTPResponse& response) const {
    // Use the response's serialize method
    return response.serialize().size();
}

memory::ZeroCopyBuffer HTTPResponseSerializer::serialize(const HTTPResponse& response) {
    return response.serialize();
}

// StreamingHTTPParser implementation
StreamingHTTPParser::StreamingHTTPParser() {
}

StreamingHTTPParser::~StreamingHTTPParser() {
}

bool StreamingHTTPParser::parse(const char* data, size_t size) {
    return parser_.parse_request(data, size);
}

void StreamingHTTPParser::set_on_request_line(std::function<void(HTTPMethod, const std::string&, HTTPVersion)> callback) {
    events_.on_request_line = callback;
}

void StreamingHTTPParser::set_on_header(std::function<void(const std::string&, const std::string&)> callback) {
    events_.on_header = callback;
}

void StreamingHTTPParser::set_on_body(DataCallback callback) {
    events_.on_body = callback;
}

void StreamingHTTPParser::set_on_complete(std::function<void()> callback) {
    events_.on_complete = callback;
}

void StreamingHTTPParser::set_on_error(std::function<void(const std::string&)> callback) {
    events_.on_error = callback;
}

void StreamingHTTPParser::reset() {
    parser_.reset();
}

} // namespace network
} // namespace best_server