// DNS Resolver Implementation
// Async DNS resolution with caching support

#include <best_server/network/dns/dns_resolver.hpp>
#include <best_server/core/scheduler.hpp>
#include <best_server/io/udp_socket.hpp>
#include <algorithm>
#include <cstring>
#include <random>
#include <arpa/inet.h>
#include <netdb.h>
#include <sys/socket.h>
#include <unistd.h>

namespace best_server {
namespace network {
namespace dns {

namespace {
    // DNS header structure
    struct __attribute__((packed)) DNSHeader {
        uint16_t id;
        uint16_t flags;
        uint16_t qdcount;
        uint16_t ancount;
        uint16_t nscount;
        uint16_t arcount;
    };
    
    // DNS question structure
    struct __attribute__((packed)) DNSQuestion {
        uint16_t type;
        uint16_t cls;
    };
    
    // DNS resource record structure
    struct __attribute__((packed)) DNSRR {
        uint16_t type;
        uint16_t cls;
        uint32_t ttl;
        uint16_t rdlength;
    };
    
    // Default DNS servers
    const std::vector<std::string> DEFAULT_NAMESERVERS = {
        "8.8.8.8",        // Google DNS
        "8.8.4.4",        // Google DNS
        "1.1.1.1",        // Cloudflare DNS
        "1.0.0.1"         // Cloudflare DNS
    };
    
    // DNS port
    [[maybe_unused]] const uint16_t DNS_PORT = 53;
    
    // Maximum DNS packet size
    [[maybe_unused]] const size_t MAX_DNS_PACKET_SIZE = 512;
    
    // Generate random query ID
    [[maybe_unused]] uint16_t generate_query_id() {
        static std::random_device rd;
        static std::mt19937 gen(rd());
        static std::uniform_int_distribution<uint16_t> dist(1, 65535);
        return dist(gen);
    }
}

// Record implementation
std::string Record::ipv4_address() const {
    if (type == RecordType::A) {
        return data;
    }
    return "";
}

std::string Record::ipv6_address() const {
    if (type == RecordType::AAAA) {
        return data;
    }
    return "";
}

uint16_t Record::priority() const {
    if (type == RecordType::MX || type == RecordType::SRV) {
        if (data.size() >= 2) {
            return (static_cast<uint16_t>(data[0]) << 8) | data[1];
        }
    }
    return 0;
}

// QueryResult implementation
Record QueryResult::first_record() const {
    if (records.empty()) {
        return Record{};
    }
    return records[0];
}

std::vector<std::string> QueryResult::ipv4_addresses() const {
    std::vector<std::string> result;
    for (const auto& record : records) {
        if (record.type == RecordType::A) {
            result.push_back(record.ipv4_address());
        }
    }
    return result;
}

std::vector<std::string> QueryResult::ipv6_addresses() const {
    std::vector<std::string> result;
    for (const auto& record : records) {
        if (record.type == RecordType::AAAA) {
            result.push_back(record.ipv6_address());
        }
    }
    return result;
}

// Resolver implementation
Resolver::Resolver(const ResolverConfig& config)
    : config_(config) {
    // Set default nameservers if none provided
    if (config_.nameservers.empty()) {
        config_.nameservers = DEFAULT_NAMESERVERS;
    }
}

Resolver::Ptr Resolver::create(const ResolverConfig& config) {
    return std::shared_ptr<Resolver>(new Resolver(config));
}

future::Future<QueryResult> Resolver::query(const std::string& name,
                                             RecordType type,
                                             QueryClass cls) {
    // For now, query synchronously and return ready future
    return future::make_future<QueryResult>([this, name, type, cls]() {
        return query_internal_sync(name, type, cls);
    });
}

future::Future<QueryResult> Resolver::resolve(const std::string& hostname) {
    return query(hostname, RecordType::A);
}

future::Future<QueryResult> Resolver::reverse_lookup(const std::string& ip_address) {
    // Convert IP to in-addr.arpa format
    std::string arpa_name;
    
    if (utils::is_ipv4(ip_address)) {
        uint8_t addr[4];
        if (utils::parse_ipv4(ip_address, addr)) {
            arpa_name = std::to_string(addr[3]) + "." +
                       std::to_string(addr[2]) + "." +
                       std::to_string(addr[1]) + "." +
                       std::to_string(addr[0]) + ".in-addr.arpa";
        }
    } else if (utils::is_ipv6(ip_address)) {
        // IPv6 reverse lookup (not implemented for brevity)
        QueryResult result;
        result.success = false;
        result.error_message = "IPv6 reverse lookup not implemented";
        return future::make_ready_future(std::move(result));
    } else {
        QueryResult result;
        result.success = false;
        result.error_message = "Invalid IP address";
        return future::make_ready_future(std::move(result));
    }
    
    return query(arpa_name, RecordType::PTR);
}

future::Future<QueryResult> Resolver::resolve_service(const std::string& service,
                                                       const std::string& proto,
                                                       const std::string& domain) {
    std::string srv_name = "_" + service + "._" + proto + "." + domain;
    return query(srv_name, RecordType::SRV);
}

QueryResult Resolver::query_internal_sync(const std::string& name,
                                            RecordType type,
                                            QueryClass cls) {
    QueryResult result;
        
        // Normalize hostname
        std::string normalized = utils::normalize_hostname(name);
        
        // Check cache
        std::string cache_key = utils::build_cache_key(normalized, type, cls);
        if (config_.enable_cache) {
            auto cached = check_cache(cache_key);
            if (cached) {
                return *cached;
            }
        }
        
        // Try using getaddrinfo first (system resolver)
        struct addrinfo hints, *res;
        memset(&hints, 0, sizeof(hints));
        
        if (type == RecordType::A) {
            hints.ai_family = AF_INET;
        } else if (type == RecordType::AAAA) {
            hints.ai_family = AF_INET6;
        } else {
            hints.ai_family = AF_UNSPEC;
        }
        
        hints.ai_socktype = SOCK_STREAM;
        
        int ret = getaddrinfo(normalized.c_str(), nullptr, &hints, &res);
        
        if (ret == 0 && res != nullptr) {
            result.success = true;
            
            for (struct addrinfo* p = res; p != nullptr; p = p->ai_next) {
                Record record;
                record.name = normalized;
                record.type = type;
                record.ttl = config_.cache_ttl;
                
                char addr_str[INET6_ADDRSTRLEN];
                if (p->ai_family == AF_INET) {
                    struct sockaddr_in* addr_in = reinterpret_cast<struct sockaddr_in*>(p->ai_addr);
                    inet_ntop(AF_INET, &addr_in->sin_addr, addr_str, sizeof(addr_str));
                    record.data = addr_str;
                    record.type = RecordType::A;
                } else if (p->ai_family == AF_INET6) {
                    struct sockaddr_in6* addr_in6 = reinterpret_cast<struct sockaddr_in6*>(p->ai_addr);
                    inet_ntop(AF_INET6, &addr_in6->sin6_addr, addr_str, sizeof(addr_str));
                    record.data = addr_str;
                    record.type = RecordType::AAAA;
                }
                
                if (!record.data.empty()) {
                    result.records.push_back(record);
                }
            }
            
            freeaddrinfo(res);
        } else {
            // Fall back to direct DNS query
            // (This would require implementing the full DNS protocol)
            result.success = false;
            result.error_message = gai_strerror(ret);
        }
        
        // Update cache
        if (config_.enable_cache && result.success) {
            update_cache(cache_key, result);
        }
        
        return result;
}

std::optional<QueryResult> Resolver::check_cache(const std::string& key) {
    std::unique_lock<std::mutex> lock(cache_mutex_);
    
    auto it = cache_.find(key);
    if (it != cache_.end()) {
        auto& entry = it->second;
        
        // Check if entry is still valid
        if (std::chrono::steady_clock::now() < entry.expires_at) {
            cache_stats_.hits++;
            return entry.result;
        } else {
            // Remove expired entry
            cache_.erase(it);
        }
    }
    
    cache_stats_.misses++;
    return std::nullopt;
}

void Resolver::update_cache(const std::string& key, const QueryResult& result) {
    std::unique_lock<std::mutex> lock(cache_mutex_);
    
    CacheEntry entry;
    entry.result = result;
    entry.expires_at = std::chrono::steady_clock::now() +
                      std::chrono::seconds(config_.cache_ttl);
    
    cache_[key] = entry;
    cache_stats_.entries.store(cache_.size());
}

void Resolver::clear_cache() {
    std::unique_lock<std::mutex> lock(cache_mutex_);
    cache_.clear();
    cache_stats_.entries = 0;
}

Resolver::CacheStats Resolver::cache_stats() const {
    std::unique_lock<std::mutex> lock(cache_mutex_);
    CacheStats stats;
    stats.entries = cache_stats_.entries.load();
    stats.hits = cache_stats_.hits.load();
    stats.misses = cache_stats_.misses.load();
    stats.evictions = cache_stats_.evictions.load();
    return stats;
}

// Utility functions
namespace utils {

bool parse_ipv4(const std::string& str, uint8_t (&addr)[4]) {
    int a, b, c, d;
    if (sscanf(str.c_str(), "%d.%d.%d.%d", &a, &b, &c, &d) != 4) {
        return false;
    }
    
    if (a < 0 || a > 255 || b < 0 || b > 255 ||
        c < 0 || c > 255 || d < 0 || d > 255) {
        return false;
    }
    
    addr[0] = static_cast<uint8_t>(a);
    addr[1] = static_cast<uint8_t>(b);
    addr[2] = static_cast<uint8_t>(c);
    addr[3] = static_cast<uint8_t>(d);
    
    return true;
}

bool parse_ipv6(const std::string& str, uint8_t (&addr)[16]) {
    struct in6_addr in6;
    if (inet_pton(AF_INET6, str.c_str(), &in6) != 1) {
        return false;
    }
    
    memcpy(addr, in6.s6_addr, 16);
    return true;
}

std::string format_ipv4(const uint8_t addr[4]) {
    char buf[16];
    snprintf(buf, sizeof(buf), "%d.%d.%d.%d",
             addr[0], addr[1], addr[2], addr[3]);
    return buf;
}

std::string format_ipv6(const uint8_t addr[16]) {
    char buf[INET6_ADDRSTRLEN];
    inet_ntop(AF_INET6, addr, buf, sizeof(buf));
    return buf;
}

bool is_ipv4(const std::string& str) {
    uint8_t addr[4];
    return parse_ipv4(str, addr);
}

bool is_ipv6(const std::string& str) {
    uint8_t addr[16];
    return parse_ipv6(str, addr);
}

bool is_ip_address(const std::string& str) {
    return is_ipv4(str) || is_ipv6(str);
}

bool is_valid_hostname(const std::string& hostname) {
    if (hostname.empty() || hostname.length() > 253) {
        return false;
    }
    
    // Check each label
    size_t start = 0;
    while (start < hostname.length()) {
        size_t end = hostname.find('.', start);
        if (end == std::string::npos) {
            end = hostname.length();
        }
        
        std::string label = hostname.substr(start, end - start);
        
        if (label.empty() || label.length() > 63) {
            return false;
        }
        
        // Check label characters
        for (char c : label) {
            if (!((c >= 'a' && c <= 'z') ||
                  (c >= 'A' && c <= 'Z') ||
                  (c >= '0' && c <= '9') ||
                  c == '-')) {
                return false;
            }
        }
        
        // Label cannot start or end with hyphen
        if (label[0] == '-' || label.back() == '-') {
            return false;
        }
        
        start = end + 1;
    }
    
    return true;
}

std::string normalize_hostname(const std::string& hostname) {
    std::string result = hostname;
    
    // Convert to lowercase
    std::transform(result.begin(), result.end(), result.begin(), ::tolower);
    
    // Remove trailing dot
    if (!result.empty() && result.back() == '.') {
        result.pop_back();
    }
    
    return result;
}

std::string build_cache_key(const std::string& name,
                             RecordType type,
                             QueryClass cls) {
    return name + ":" + std::to_string(static_cast<uint16_t>(type)) +
           ":" + std::to_string(static_cast<uint16_t>(cls));
}

bool parse_response(const std::vector<uint8_t>& data,
                    std::vector<Record>& records) {
    // Parse DNS response (simplified implementation)
    // A full implementation would parse the DNS packet format
    
    if (data.size() < sizeof(DNSHeader)) {
        return false;
    }
    
    const DNSHeader* header = reinterpret_cast<const DNSHeader*>(data.data());
    
    // Check response flag
    if (!(ntohs(header->flags) & 0x8000)) {
        return false; // Not a response
    }
    
    // Check for errors
    uint16_t rcode = ntohs(header->flags) & 0xF;
    if (rcode != 0) {
        return false;
    }
    
    // Parse answer records (simplified)
    size_t pos = sizeof(DNSHeader);
    
    // Skip question section
    uint16_t qdcount = ntohs(header->qdcount);
    for (uint16_t i = 0; i < qdcount && pos < data.size(); ++i) {
        // Skip domain name
        while (pos < data.size() && data[pos] != 0) {
            uint8_t len = data[pos];
            if (len == 0) break;
            pos += 1 + len;
        }
        if (pos < data.size()) pos++; // Skip null terminator
        
        // Skip type and class
        if (pos + 4 > data.size()) break;
        pos += 4;
    }
    
    // Parse answer records
    uint16_t ancount = ntohs(header->ancount);
    for (uint16_t i = 0; i < ancount && pos < data.size(); ++i) {
        // Skip domain name (or pointer)
        while (pos < data.size() && data[pos] != 0) {
            if ((data[pos] & 0xC0) == 0xC0) {
                // Name pointer
                pos += 2;
                break;
            }
            uint8_t len = data[pos];
            if (len == 0) break;
            pos += 1 + len;
        }
        if (pos < data.size()) pos++; // Skip null terminator
        
        // Read record header
        if (pos + sizeof(DNSRR) > data.size()) break;
        
        const DNSRR* rr = reinterpret_cast<const DNSRR*>(&data[pos]);
        pos += sizeof(DNSRR);
        
        Record record;
        record.type = static_cast<RecordType>(ntohs(rr->type));
        record.ttl = ntohl(rr->ttl);
        
        uint16_t rdlength = ntohs(rr->rdlength);
        
        if (pos + rdlength > data.size()) break;
        
        // Parse record data based on type
        if (record.type == RecordType::A && rdlength == 4) {
            uint8_t addr[4];
            memcpy(addr, &data[pos], 4);
            record.data = format_ipv4(addr);
        } else if (record.type == RecordType::AAAA && rdlength == 16) {
            uint8_t addr[16];
            memcpy(addr, &data[pos], 16);
            record.data = format_ipv6(addr);
        } else if (record.type == RecordType::CNAME || record.type == RecordType::PTR) {
            // Parse domain name
            std::string name;
            size_t name_pos = pos;
            while (name_pos < pos + rdlength) {
                if (data[name_pos] & 0xC0) {
                    // Pointer (not fully implemented)
                    break;
                }
                uint8_t len = data[name_pos];
                if (len == 0) break;
                if (!name.empty()) name += ".";
                name.append(reinterpret_cast<const char*>(&data[name_pos + 1]), len);
                name_pos += 1 + len;
            }
            record.data = name;
        } else {
            // Raw data
            record.data.assign(reinterpret_cast<const char*>(&data[pos]), rdlength);
        }
        
        pos += rdlength;
        records.push_back(record);
    }
    
    return true;
}

} // namespace utils

// Global resolver
Resolver& default_resolver() {
    static Resolver::Ptr resolver = Resolver::create();
    return *resolver;
}

} // namespace dns
} // namespace network
} // namespace best_server