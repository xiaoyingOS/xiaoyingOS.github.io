#pragma once

#include <best_server/future/future.hpp>
#include <string>
#include <vector>
#include <memory>
#include <chrono>
#include <list>
#include <mutex>

namespace best_server {
namespace network {
namespace dns {

// DNS record types
enum class RecordType : uint16_t {
    A = 1,      // IPv4 address
    AAAA = 28,  // IPv6 address
    CNAME = 5,  // Canonical name
    MX = 15,    // Mail exchange
    TXT = 16,   // Text record
    NS = 2,     // Name server
    PTR = 12,   // Pointer record
    SRV = 33,   // Service record
    ANY = 255   // Any record
};

// DNS query class
enum class QueryClass : uint16_t {
    IN = 1,     // Internet
    CH = 3,     // Chaos
    HS = 4,     // Hesiod
    ANY = 255   // Any
};

// DNS record
struct Record {
    std::string name;
    RecordType type;
    uint32_t ttl;
    std::string data;
    
    // Convenience accessors
    std::string as_string() const { return data; }
    
    // IPv4 address for A records
    std::string ipv4_address() const;
    
    // IPv6 address for AAAA records
    std::string ipv6_address() const;
    
    // Priority for MX/SRV records
    uint16_t priority() const;
};

// DNS query result
struct QueryResult {
    bool success{false};
    std::string error_message;
    std::vector<Record> records;
    
    explicit operator bool() const { return success; }
    
    // Get first record
    Record first_record() const;
    
    // Get all A records
    std::vector<std::string> ipv4_addresses() const;
    
    // Get all AAAA records
    std::vector<std::string> ipv6_addresses() const;
};

// DNS resolver configuration
struct ResolverConfig {
    std::vector<std::string> nameservers;  // DNS servers (e.g., "8.8.8.8")
    uint32_t timeout{5000};                // Query timeout in milliseconds
    uint32_t retries{3};                   // Number of retries
    bool enable_cache{true};               // Enable DNS caching
    uint32_t cache_ttl{300};               // Cache TTL in seconds
    bool enable_ipv6{true};                // Enable IPv6 queries
};

// DNS resolver (async)
class Resolver {
public:
    using Ptr = std::shared_ptr<Resolver>;
    
    static Ptr create(const ResolverConfig& config = ResolverConfig());
    
    // Query DNS records
    future::Future<QueryResult> query(const std::string& name,
                                      RecordType type = RecordType::A,
                                      QueryClass cls = QueryClass::IN);
    
    // Resolve hostname to IP addresses
    future::Future<QueryResult> resolve(const std::string& hostname);
    
    // Reverse DNS lookup (IP to hostname)
    future::Future<QueryResult> reverse_lookup(const std::string& ip_address);
    
    // Resolve service discovery (SRV record)
    future::Future<QueryResult> resolve_service(const std::string& service,
                                                 const std::string& proto,
                                                 const std::string& domain);
    
    // Get configuration
    const ResolverConfig& config() const { return config_; }
    
    // Clear DNS cache
    void clear_cache();
    
    // Get cache statistics
    struct CacheStats {
        size_t entries{0};
        size_t hits{0};
        size_t misses{0};
        size_t evictions{0};
    };
    CacheStats cache_stats() const;
    
private:
    // Internal cache statistics (atomic)
    struct InternalCacheStats {
        std::atomic<size_t> entries{0};
        std::atomic<size_t> hits{0};
        std::atomic<size_t> misses{0};
        std::atomic<size_t> evictions{0};
    };
    Resolver(const ResolverConfig& config);
    
    // Internal query implementation
    QueryResult query_internal_sync(const std::string& name,
                                     RecordType type,
                                     QueryClass cls);
    
    // Check cache (lock-free read path)
    std::optional<QueryResult> check_cache(const std::string& key);
    
    // Update cache (lock-fast write path)
    void update_cache(const std::string& key, const QueryResult& result);
    
    // Clean expired entries
    void clean_expired_entries();
    
    // Evict entries based on LRU policy
    void evict_lru_entries(size_t target_size);
    
    ResolverConfig config_;
    
    // DNS cache with LRU support
    struct CacheEntry {
        QueryResult result;
        std::chrono::steady_clock::time_point expires_at;
        std::chrono::steady_clock::time_point last_accessed;
        uint32_t access_count{0};
    };
    
    // LRU list for cache eviction
    std::unordered_map<std::string, CacheEntry> cache_;
    std::list<std::string> lru_list_;  // Most recently used at front
    mutable std::mutex cache_mutex_;
    InternalCacheStats cache_stats_;
    
    static constexpr size_t MAX_CACHE_ENTRIES = 10000;
    std::chrono::steady_clock::time_point last_cleanup_;
};

// DNS utilities
namespace utils {
    // Parse IP address
    bool parse_ipv4(const std::string& str, uint8_t (&addr)[4]);
    bool parse_ipv6(const std::string& str, uint8_t (&addr)[16]);
    
    // Format IP address
    std::string format_ipv4(const uint8_t addr[4]);
    std::string format_ipv6(const uint8_t addr[16]);
    
    // Check if string is IP address
    bool is_ipv4(const std::string& str);
    bool is_ipv6(const std::string& str);
    bool is_ip_address(const std::string& str);
    
    // Validate hostname
    bool is_valid_hostname(const std::string& hostname);
    
    // Normalize hostname
    std::string normalize_hostname(const std::string& hostname);
    
    // Build cache key
    std::string build_cache_key(const std::string& name,
                               RecordType type,
                               QueryClass cls);
    
    // Parse DNS response
    bool parse_response(const std::vector<uint8_t>& data,
                        std::vector<Record>& records);
}

// Global resolver (singleton)
Resolver& default_resolver();

} // namespace dns
} // namespace network
} // namespace best_server