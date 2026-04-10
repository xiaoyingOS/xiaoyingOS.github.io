// HTTPRequest implementation
#include "best_server/network/http_request.hpp"
#include <sstream>
#include <algorithm>
#include <regex>
#include <cctype>

namespace best_server {
namespace network {

// HTTP method conversion - optimized with array lookup
static constexpr const char* METHOD_STRINGS[] = {
    "GET",      // 0
    "POST",     // 1
    "PUT",      // 2
    "DELETE",   // 3
    "PATCH",    // 4
    "HEAD",     // 5
    "OPTIONS",  // 6
    "TRACE",    // 7
    "CONNECT"   // 8
};

static constexpr size_t NUM_METHODS = sizeof(METHOD_STRINGS) / sizeof(METHOD_STRINGS[0]);

std::string method_to_string(HTTPMethod method) {
    int idx = static_cast<int>(method);
    if (idx >= 0 && idx < static_cast<int>(NUM_METHODS)) {
        return METHOD_STRINGS[idx];
    }
    return "UNKNOWN";
}

// Optimized string_to_method using direct comparison without transformation
HTTPMethod string_to_method(const std::string& str) {
    // Fast path: check uppercase versions first (most common)
    if (str == "GET") return HTTPMethod::GET;
    if (str == "POST") return HTTPMethod::POST;
    if (str == "PUT") return HTTPMethod::PUT;
    if (str == "DELETE") return HTTPMethod::DELETE;
    if (str == "PATCH") return HTTPMethod::PATCH;
    if (str == "HEAD") return HTTPMethod::HEAD;
    if (str == "OPTIONS") return HTTPMethod::OPTIONS;
    if (str == "TRACE") return HTTPMethod::TRACE;
    if (str == "CONNECT") return HTTPMethod::CONNECT;
    
    // Slow path: try lowercase
    if (str == "get") return HTTPMethod::GET;
    if (str == "post") return HTTPMethod::POST;
    if (str == "put") return HTTPMethod::PUT;
    if (str == "delete") return HTTPMethod::DELETE;
    if (str == "patch") return HTTPMethod::PATCH;
    if (str == "head") return HTTPMethod::HEAD;
    if (str == "options") return HTTPMethod::OPTIONS;
    if (str == "trace") return HTTPMethod::TRACE;
    if (str == "connect") return HTTPMethod::CONNECT;
    
    return HTTPMethod::GET;  // Default
}

// HTTPVersion implementation
std::string HTTPVersion::to_string() const {
    return "HTTP/" + std::to_string(major) + "." + std::to_string(minor);
}

// HTTPRequest implementation
HTTPRequest::HTTPRequest()
    : method_(HTTPMethod::GET)
    , version_(1, 1)
    , body_(1024 * 1024)  // 初始容量 1MB，原来是 64KB，减少大文件扩展次数
{
}

HTTPRequest::~HTTPRequest() {
}

// Header operations - optimized with lowercase normalization on set
void HTTPRequest::set_header(const std::string& name, const std::string& value) {
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

std::string HTTPRequest::get_header(const std::string& name, const std::string& default_value) const {
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

bool HTTPRequest::has_header(const std::string& name) const {
    return !get_header(name).empty();
}

void HTTPRequest::remove_header(const std::string& name) {
    auto it = headers_.find(name);
    if (it != headers_.end()) {
        headers_.erase(it);
    }
}

// Body operations
void HTTPRequest::set_body(memory::ZeroCopyBuffer&& body) {
    body_ = std::move(body);
    body_string_.clear();
}

const std::string& HTTPRequest::body_string() const {
    if (body_string_.empty() && body_.size() > 0) {
        body_string_.assign(body_.data(), body_.size());
    }
    return body_string_;
}

// Query parameter operations
std::string HTTPRequest::get_query_param(const std::string& name, const std::string& default_value) const {
    auto it = query_params_.find(name);
    if (it != query_params_.end()) {
        return it->second;
    }
    return default_value;
}

bool HTTPRequest::has_query_param(const std::string& name) const {
    return query_params_.find(name) != query_params_.end();
}

// Cookie operations
std::string HTTPRequest::get_cookie(const std::string& name, const std::string& default_value) const {
    auto it = cookies_.find(name);
    if (it != cookies_.end()) {
        return it->second;
    }
    return default_value;
}

bool HTTPRequest::has_cookie(const std::string& name) const {
    return cookies_.find(name) != cookies_.end();
}

// Content length
size_t HTTPRequest::content_length() const {
    auto len_str = get_header("Content-Length");
    if (!len_str.empty()) {
        try {
            return static_cast<size_t>(std::stoull(len_str));
        } catch (...) {
            return body_.size();
        }
    }
    return body_.size();
}

// Keep-alive check
bool HTTPRequest::keep_alive() const {
    // HTTP/1.1 defaults to keep-alive
    if (version_.major == 1 && version_.minor >= 1) {
        auto conn = get_header("Connection");
        if (conn.empty() || conn == "keep-alive") {
            return true;
        }
        return false;
    }
    
    // HTTP/1.0 defaults to close
    auto conn = get_header("Connection");
    return !conn.empty() && conn == "keep-alive";
}

// Parse query string
void HTTPRequest::parse_query_string() {
    query_params_.clear();
    
    if (query_string_.empty()) {
        return;
    }
    
    size_t pos = 0;
    while (pos < query_string_.size()) {
        // Find parameter separator
        size_t end = query_string_.find('&', pos);
        if (end == std::string::npos) {
            end = query_string_.size();
        }
        
        // Find name-value separator
        size_t eq = query_string_.find('=', pos);
        if (eq != std::string::npos && eq < end) {
            std::string name = query_string_.substr(pos, eq - pos);
            std::string value = query_string_.substr(eq + 1, end - eq - 1);
            
            // URL decode name
            for (size_t i = 0; i < name.size(); ++i) {
                if (name[i] == '+' && i < name.size()) name[i] = ' ';
                if (name[i] == '%' && i + 2 < name.size()) {
                    int hex;
                    sscanf(name.substr(i + 1, 2).c_str(), "%x", &hex);
                    name.replace(i, 3, 1, static_cast<char>(hex));
                }
            }
            
            // URL decode value
            for (size_t i = 0; i < value.size(); ++i) {
                if (value[i] == '+' && i < value.size()) value[i] = ' ';
                if (value[i] == '%' && i + 2 < value.size()) {
                    int hex;
                    sscanf(value.substr(i + 1, 2).c_str(), "%x", &hex);
                    value.replace(i, 3, 1, static_cast<char>(hex));
                }
            }
            
            query_params_[name] = value;
        } else if (eq == std::string::npos) {
            // Parameter without value
            std::string name = query_string_.substr(pos, end - pos);
            query_params_[name] = "";
        }
        
        pos = end + 1;
    }
}

// Parse cookies
void HTTPRequest::parse_cookies() {
    cookies_.clear();
    
    auto cookie_header = get_header("Cookie");
    if (cookie_header.empty()) {
        return;
    }
    
    size_t pos = 0;
    while (pos < cookie_header.size()) {
        // Skip whitespace
        while (pos < cookie_header.size() && ::isspace(cookie_header[pos])) {
            ++pos;
        }
        
        if (pos >= cookie_header.size()) {
            break;
        }
        
        // Find cookie separator
        size_t end = cookie_header.find(';', pos);
        if (end == std::string::npos) {
            end = cookie_header.size();
        }
        
        // Find name-value separator
        size_t eq = cookie_header.find('=', pos);
        if (eq != std::string::npos && eq < end) {
            std::string name = cookie_header.substr(pos, eq - pos);
            std::string value = cookie_header.substr(eq + 1, end - eq - 1);
            
            // Trim name
            while (!name.empty() && ::isspace(name.back())) {
                name.pop_back();
            }
            
            // Trim value
            while (!value.empty() && ::isspace(value.back())) {
                value.pop_back();
            }
            
            cookies_[name] = value;
        }
        
        pos = end + 1;
    }
}

// Get Range header
HTTPRequest::Range HTTPRequest::get_range() const {
    Range range{0, 0, 0, false};
    
    auto range_header = get_header("Range");
    if (range_header.empty()) {
        return range;
    }
    
    // Parse "bytes=start-end" format
    const std::string prefix = "bytes=";
    if (range_header.compare(0, prefix.size(), prefix) != 0) {
        return range;
    }
    
    std::string range_spec = range_header.substr(prefix.size());
    
    // Find the dash
    size_t dash_pos = range_spec.find('-');
    if (dash_pos == std::string::npos) {
        return range;
    }
    
    try {
        std::string start_str = range_spec.substr(0, dash_pos);
        std::string end_str = range_spec.substr(dash_pos + 1);
        
        if (!start_str.empty()) {
            range.start = std::stoull(start_str);
        }
        
        if (!end_str.empty()) {
            range.end = std::stoull(end_str);
        }
        
        if (!start_str.empty() && !end_str.empty()) {
            range.size = range.end - range.start + 1;
        } else if (!start_str.empty()) {
            // bytes=start- means from start to end of file
            range.end = std::numeric_limits<size_t>::max();
            range.size = std::numeric_limits<size_t>::max() - range.start;
        } else {
            // bytes=-suffix means last suffix bytes
            size_t suffix = std::stoull(end_str);
            range.end = std::numeric_limits<size_t>::max();
            range.start = std::numeric_limits<size_t>::max() - suffix;
            range.size = suffix;
        }
        
        range.valid = true;
    } catch (...) {
        // Invalid range format
        range.valid = false;
    }
    
    return range;
}

// Parse multipart form data
std::vector<HTTPRequest::FormField> HTTPRequest::parse_multipart_form_data() const {
    std::vector<FormField> fields;
    
    auto content_type = get_header("Content-Type");
    if (content_type.find("multipart/form-data") == std::string::npos) {
        return fields;
    }
    
    // Extract boundary
    size_t boundary_pos = content_type.find("boundary=");
    if (boundary_pos == std::string::npos) {
        return fields;
    }
    
    std::string boundary = content_type.substr(boundary_pos + 9);
    
    std::string body = body_string();
    std::string delimiter = "--" + boundary;
    
    size_t pos = 0;
    while (pos < body.size()) {
        // Find boundary
        size_t delim_pos = body.find(delimiter, pos);
        if (delim_pos == std::string::npos || delim_pos > pos) {
            break;
        }
        
        // Skip delimiter and CRLF
        pos = delim_pos + delimiter.size();
        if (pos + 2 > body.size() || body.substr(pos, 2) != "\r\n") {
            break;
        }
        pos += 2;
        
        // Find end of part
        size_t end_pos = body.find(delimiter, pos);
        if (end_pos == std::string::npos) {
            end_pos = body.size();
        }
        
        // Extract part content
        std::string part = body.substr(pos, end_pos - pos);
        
        // Parse part headers
        FormField field;
        size_t header_end = part.find("\r\n\r\n");
        if (header_end != std::string::npos) {
            std::string headers = part.substr(0, header_end);
            std::string content = part.substr(header_end + 4);
            
            // Extract Content-Disposition
            size_t disp_pos = headers.find("Content-Disposition:");
            if (disp_pos != std::string::npos) {
                size_t disp_end = headers.find("\r\n", disp_pos);
                std::string disp = headers.substr(disp_pos + 20, disp_end - disp_pos - 20);
                
                // Extract name
                size_t name_pos = disp.find("name=\"");
                if (name_pos != std::string::npos) {
                    size_t name_end = disp.find("\"", name_pos + 6);
                    if (name_end != std::string::npos) {
                        field.name = disp.substr(name_pos + 6, name_end - name_pos - 6);
                    }
                }
                
                // Extract filename
                size_t filename_pos = disp.find("filename=\"");
                if (filename_pos != std::string::npos) {
                    size_t filename_end = disp.find("\"", filename_pos + 10);
                    if (filename_end != std::string::npos) {
                        field.filename = disp.substr(filename_pos + 10, filename_end - filename_pos - 10);
                    }
                }
            }
            
            // Extract Content-Type
            size_t type_pos = headers.find("Content-Type:");
            if (type_pos != std::string::npos) {
                size_t type_end = headers.find("\r\n", type_pos);
                if (type_end != std::string::npos) {
                    field.content_type = headers.substr(type_pos + 13, type_end - type_pos - 13);
                }
            }
            
            // Copy content to buffer
            field.data = memory::ZeroCopyBuffer(content.size());
            field.data.write(content.data(), content.size());
        }
        
        fields.push_back(field);
        pos = end_pos;
    }
    
    return fields;
}

// Reset request
void HTTPRequest::reset() {
    method_ = HTTPMethod::GET;
    url_.clear();
    path_.clear();
    query_string_.clear();
    version_ = HTTPVersion(1, 1);
    headers_.clear();
    body_.clear();
    body_string_.clear();
    query_params_.clear();
    cookies_.clear();
    remote_address_.clear();
}

// RequestBuilder implementation
RequestBuilder::RequestBuilder() {
}

RequestBuilder& RequestBuilder::method(HTTPMethod method) {
    request_.set_method(method);
    return *this;
}

RequestBuilder& RequestBuilder::url(const std::string& url) {
    request_.set_url(url);
    
    // Parse URL to extract path and query string
    size_t query_pos = url.find('?');
    if (query_pos != std::string::npos) {
        request_.set_path(url.substr(0, query_pos));
        request_.set_query_string(url.substr(query_pos + 1));
    } else {
        request_.set_path(url);
    }
    
    return *this;
}

RequestBuilder& RequestBuilder::header(const std::string& name, const std::string& value) {
    request_.set_header(name, value);
    return *this;
}

RequestBuilder& RequestBuilder::body(const std::string& body) {
    memory::ZeroCopyBuffer buffer(body.size());
    buffer.write(body.data(), body.size());
    request_.set_body(std::move(buffer));
    return *this;
}

RequestBuilder& RequestBuilder::body(memory::ZeroCopyBuffer&& body) {
    request_.set_body(std::move(body));
    return *this;
}

RequestBuilder& RequestBuilder::query_param(const std::string& name, const std::string& value) {
    std::string query = request_.query_string();
    if (!query.empty()) {
        query += "&";
    }
    query += name + "=" + value;
    request_.set_query_string(query);
    return *this;
}

RequestBuilder& RequestBuilder::cookie(const std::string& name, const std::string& value) {
    std::string cookie = request_.get_header("Cookie");
    if (!cookie.empty()) {
        cookie += "; ";
    }
    cookie += name + "=" + value;
    request_.set_header("Cookie", cookie);
    return *this;
}

HTTPRequest RequestBuilder::build() const {
    HTTPRequest req = request_;
    req.parse_query_string();
    req.parse_cookies();
    return req;
}

} // namespace network
} // namespace best_server