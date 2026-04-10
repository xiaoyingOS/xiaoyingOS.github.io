#pragma once

#include <best_server/future/future.hpp>
#include <best_server/io/tcp_socket.hpp>
#include <string>
#include <vector>
#include <variant>
#include <memory>
#include <unordered_map>

namespace best_server {
namespace database {
namespace redis {

// Redis data types
enum class DataType {
    None,
    String,
    List,
    Set,
    SortedSet,
    Hash,
    Stream,
    Bitmap,
    HyperLogLog,
    Geo
};

// Forward declaration for recursive variant
struct RedisValue;

// Redis value wrapper for recursive support
struct RedisArray {
    std::vector<RedisValue> values;
};

struct RedisMap {
    std::unordered_map<std::string, RedisValue> entries;
};

// Redis value (variant of supported types)
struct RedisValue : std::variant<std::monostate,
                                  std::string,
                                  int64_t,
                                  double,
                                  RedisArray,
                                  RedisMap> {
    using Base = std::variant<std::monostate,
                              std::string,
                              int64_t,
                              double,
                              RedisArray,
                              RedisMap>;
    
    using Base::Base;
    using Base::operator=;
};

// Redis result
struct RedisResult {
    bool success{false};
    std::string error_message;
    RedisValue value;
    DataType type{DataType::None};
    
    explicit operator bool() const { return success; }
    
    // Convenience accessors
    bool is_nil() const { return std::holds_alternative<std::monostate>(value); }
    bool is_string() const { return std::holds_alternative<std::string>(value); }
    bool is_int() const { return std::holds_alternative<int64_t>(value); }
    bool is_double() const { return std::holds_alternative<double>(value); }
    bool is_array() const { return std::holds_alternative<RedisArray>(value); }
    bool is_map() const { return std::holds_alternative<RedisMap>(value); }
    
    std::string as_string() const;
    int64_t as_int() const;
    double as_double() const;
    std::vector<RedisValue> as_array() const;
    std::unordered_map<std::string, RedisValue> as_map() const;
};

// Redis configuration
struct RedisConfig {
    std::string host{"localhost"};
    uint16_t port{6379};
    std::string password;
    uint16_t database{0};
    uint32_t timeout{5000};          // Connection timeout in ms
    uint32_t pool_size{10};          // Connection pool size
    bool enable_tls{false};
    std::string tls_cert_file;
    std::string tls_key_file;
    std::string tls_ca_file;
};

// Redis command builder
class Command {
public:
    Command() = default;
    explicit Command(const std::string& name);
    
    Command& arg(const std::string& value);
    Command& arg(int64_t value);
    Command& arg(double value);
    Command& arg(const std::vector<std::string>& values);
    
    std::string build() const;
    
    const std::string& name() const { return name_; }
    const std::vector<std::string>& args() const { return args_; }
    
private:
    std::string name_;
    std::vector<std::string> args_;
};

// Redis client
class Client {
public:
    using Ptr = std::shared_ptr<Client>;
    
    static future::Future<Ptr> connect(const RedisConfig& config);
    
    ~Client();
    
    // Connection management
    future::Future<RedisResult> ping();
    future::Future<RedisResult> quit();
    future::Future<RedisResult> auth(const std::string& password);
    future::Future<RedisResult> select(uint16_t database);
    
    // String operations
    future::Future<RedisResult> get(const std::string& key);
    future::Future<RedisResult> set(const std::string& key, const std::string& value);
    future::Future<RedisResult> setex(const std::string& key, uint32_t seconds, const std::string& value);
    future::Future<RedisResult> setnx(const std::string& key, const std::string& value);
    future::Future<RedisResult> del(const std::string& key);
    future::Future<RedisResult> del(const std::vector<std::string>& keys);
    future::Future<RedisResult> exists(const std::string& key);
    future::Future<RedisResult> expire(const std::string& key, uint32_t seconds);
    future::Future<RedisResult> ttl(const std::string& key);
    future::Future<RedisResult> incr(const std::string& key);
    future::Future<RedisResult> incrby(const std::string& key, int64_t increment);
    future::Future<RedisResult> decr(const std::string& key);
    future::Future<RedisResult> decrby(const std::string& key, int64_t decrement);
    
    // List operations
    future::Future<RedisResult> lpush(const std::string& key, const std::string& value);
    future::Future<RedisResult> rpush(const std::string& key, const std::string& value);
    future::Future<RedisResult> lpop(const std::string& key);
    future::Future<RedisResult> rpop(const std::string& key);
    future::Future<RedisResult> lrange(const std::string& key, int64_t start, int64_t stop);
    future::Future<RedisResult> llen(const std::string& key);
    
    // Set operations
    future::Future<RedisResult> sadd(const std::string& key, const std::string& member);
    future::Future<RedisResult> srem(const std::string& key, const std::string& member);
    future::Future<RedisResult> smembers(const std::string& key);
    future::Future<RedisResult> sismember(const std::string& key, const std::string& member);
    future::Future<RedisResult> scard(const std::string& key);
    
    // Sorted set operations
    future::Future<RedisResult> zadd(const std::string& key, double score, const std::string& member);
    future::Future<RedisResult> zrem(const std::string& key, const std::string& member);
    future::Future<RedisResult> zrange(const std::string& key, int64_t start, int64_t stop);
    future::Future<RedisResult> zrangebyscore(const std::string& key, double min, double max);
    future::Future<RedisResult> zcard(const std::string& key);
    future::Future<RedisResult> zscore(const std::string& key, const std::string& member);
    
    // Hash operations
    future::Future<RedisResult> hset(const std::string& key, const std::string& field, const std::string& value);
    future::Future<RedisResult> hget(const std::string& key, const std::string& field);
    future::Future<RedisResult> hgetall(const std::string& key);
    future::Future<RedisResult> hdel(const std::string& key, const std::string& field);
    future::Future<RedisResult> hexists(const std::string& key, const std::string& field);
    future::Future<RedisResult> hkeys(const std::string& key);
    future::Future<RedisResult> hvals(const std::string& key);
    future::Future<RedisResult> hlen(const std::string& key);
    
    // Transaction operations
    future::Future<RedisResult> multi();
    future::Future<RedisResult> exec();
    future::Future<RedisResult> discard();
    
    // Pub/Sub operations
    future::Future<RedisResult> publish(const std::string& channel, const std::string& message);
    future::Future<RedisResult> subscribe(const std::string& channel);
    future::Future<RedisResult> unsubscribe(const std::string& channel);
    
    // Generic command
    future::Future<RedisResult> execute(const Command& command);
    future::Future<RedisResult> execute(const std::string& command);
    
    // Connection info
    const RedisConfig& config() const { return config_; }
    bool is_connected() const { return connected_; }
    
private:
    Client(const RedisConfig& config);
    
    future::Future<RedisResult> send_command(const Command& command);
    RedisResult parse_response(const std::vector<uint8_t>& data, size_t& consumed);
    
    RedisConfig config_;
    io::TCPSocket::Ptr socket_;
    bool connected_{false};
    std::vector<uint8_t> read_buffer_;
};

// Redis connection pool
class ConnectionPool {
public:
    using Ptr = std::shared_ptr<ConnectionPool>;
    
    static Ptr create(const RedisConfig& config);
    
    future::Future<Client::Ptr> acquire();
    void release(Client::Ptr client);
    
    size_t size() const;
    size_t active_count() const;
    size_t idle_count() const;
    
private:
    ConnectionPool(const RedisConfig& config);
    
    RedisConfig config_;
    std::vector<Client::Ptr> pool_;
    mutable std::mutex mutex_;
    std::condition_variable cv_;
    std::atomic<size_t> active_count_{0};
};

// Redis utilities
namespace utils {
    // RESP protocol serialization
    std::string serialize_simple_string(const std::string& value);
    std::string serialize_error(const std::string& message);
    std::string serialize_integer(int64_t value);
    std::string serialize_bulk_string(const std::string& value);
    std::string serialize_null();
    std::string serialize_array(const std::vector<std::string>& elements);
    
    // RESP protocol deserialization
    bool parse_simple_string(const std::vector<uint8_t>& data, std::string& result, size_t& consumed);
    bool parse_error(const std::vector<uint8_t>& data, std::string& result, size_t& consumed);
    bool parse_integer(const std::vector<uint8_t>& data, int64_t& result, size_t& consumed);
    bool parse_bulk_string(const std::vector<uint8_t>& data, std::string& result, size_t& consumed);
    bool parse_array(const std::vector<uint8_t>& data, std::vector<std::string>& result, size_t& consumed);
    
    // Key generation
    std::string generate_key(const std::string& prefix, const std::string& suffix);
    std::string generate_key(const std::vector<std::string>& parts);
    
    // Key pattern matching
    bool match_pattern(const std::string& pattern, const std::string& key);
}

} // namespace redis
} // namespace database
} // namespace best_server