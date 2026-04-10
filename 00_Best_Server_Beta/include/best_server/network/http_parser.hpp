// HTTPParser - High-performance HTTP request/response parser
// 
// Implements an optimized HTTP parser with:
// - Zero-copy parsing
// - Streaming support
// - HTTP/1.1 and HTTP/2 support
// - Minimal memory allocations
// - Incremental parsing

#ifndef BEST_SERVER_NETWORK_HTTP_PARSER_HPP
#define BEST_SERVER_NETWORK_HTTP_PARSER_HPP

#include <memory>
#include <functional>
#include <cstdint>
#include <string>

#include "network/http_request.hpp"
#include "network/http_response.hpp"
#include "memory/zero_copy_buffer.hpp"

namespace best_server {
namespace network {

// Parser state
enum class ParserState {
    Start,
    Method,
    URL,
    Version,
    HeaderName,
    HeaderValue,
    Body,
    ChunkSize,
    ChunkData,
    ChunkEnd,
    Complete,
    Error
};

// Parser events
struct ParserEvents {
    std::function<void(HTTPMethod, const std::string&, HTTPVersion)> on_request_line;
    std::function<void(const std::string&, const std::string&)> on_header;
    std::function<void(const char*, size_t)> on_body;
    std::function<void()> on_complete;
    std::function<void(const std::string&)> on_error;
};

// HTTP parser
class HTTPParser {
public:
    HTTPParser();
    ~HTTPParser();
    
    // Parse HTTP request
    bool parse_request(const char* data, size_t size);
    bool parse_request(const memory::ZeroCopyBuffer& buffer);
    
    // Parse HTTP response
    bool parse_response(const char* data, size_t size);
    bool parse_response(const memory::ZeroCopyBuffer& buffer);
    
    // Set event callbacks
    void set_events(ParserEvents events);
    
    // Get parsed request (only valid after complete parsing)
    HTTPRequest* request() { return request_.get(); }
    
    // Get parsed response (only valid after complete parsing)
    HTTPResponse* response() { return response_.get(); }
    
    // Check if parsing is complete
    bool is_complete() const { return state_ == ParserState::Complete; }
    
    // Check if there was an error
    bool has_error() const { return state_ == ParserState::Error; }
    
    // Get error message
    const std::string& error_message() const { return error_message_; }
    
    // Reset parser
    void reset();
    
    // Get parser state
    ParserState state() const { return state_; }
    
    // Get bytes consumed
    size_t bytes_consumed() const { return bytes_consumed_; }
    
    // Get content length
    size_t content_length() const { return content_length_; }
    
    // Check if chunked encoding
    bool is_chunked() const { return is_chunked_; }
    
private:
    // Parse methods
    bool parse_request_line(const char* data, size_t size, size_t& pos);
    bool parse_status_line(const char* data, size_t size, size_t& pos);
    bool parse_headers(const char* data, size_t size, size_t& pos);
    bool parse_body(const char* data, size_t size, size_t& pos);
    bool parse_chunk(const char* data, size_t size, size_t& pos);
    
    // Utility methods
    bool skip_whitespace(const char* data, size_t size, size_t& pos);
    bool read_token(const char* data, size_t size, size_t& pos, std::string& token, char delimiter);
    bool read_line(const char* data, size_t size, size_t& pos, std::string& line);
    bool read_until(const char* data, size_t size, size_t& pos, std::string& str, char delimiter);
    
    ParserState state_;
    ParserEvents events_;
    
    std::unique_ptr<HTTPRequest> request_;
    std::unique_ptr<HTTPResponse> response_;
    
    std::string error_message_;
    size_t bytes_consumed_;
    size_t content_length_;
    size_t body_bytes_read_;
    bool is_chunked_;
    size_t current_chunk_size_;
    size_t current_chunk_bytes_read_;
    
    // Temporary buffers
    std::string temp_buffer_;
    std::string current_header_name_;
    std::string current_header_value_;
};

// HTTP request serializer
class HTTPRequestSerializer {
public:
    HTTPRequestSerializer();
    ~HTTPRequestSerializer();
    
    // Serialize request to buffer
    memory::ZeroCopyBuffer serialize(const HTTPRequest& request);
    
    // Get required buffer size
    size_t required_size(const HTTPRequest& request) const;
    
private:
    std::string temp_buffer_;
};

// HTTP response serializer
class HTTPResponseSerializer {
public:
    HTTPResponseSerializer();
    ~HTTPResponseSerializer();
    
    // Serialize response to buffer
    memory::ZeroCopyBuffer serialize(const HTTPResponse& response);
    
    // Get required buffer size
    size_t required_size(const HTTPResponse& response) const;
    
private:
    std::string temp_buffer_;
};

// Streaming parser (for large requests/responses)
class StreamingHTTPParser {
public:
    using DataCallback = std::function<void(const char*, size_t)>;
    
    StreamingHTTPParser();
    ~StreamingHTTPParser();
    
    // Parse incrementally
    bool parse(const char* data, size_t size);
    
    // Set callbacks
    void set_on_request_line(std::function<void(HTTPMethod, const std::string&, HTTPVersion)> callback);
    void set_on_header(std::function<void(const std::string&, const std::string&)> callback);
    void set_on_body(DataCallback callback);
    void set_on_complete(std::function<void()> callback);
    void set_on_error(std::function<void(const std::string&)> callback);
    
    // Check if complete
    bool is_complete() const { return parser_.is_complete(); }
    
    // Check for error
    bool has_error() const { return parser_.has_error(); }
    
    // Reset
    void reset();
    
private:
    HTTPParser parser_;
    ParserEvents events_;
};

} // namespace network
} // namespace best_server

#endif // BEST_SERVER_NETWORK_HTTP_PARSER_HPP