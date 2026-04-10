// PostgreSQL Client - High-performance PostgreSQL database client
// 
// Implements a PostgreSQL client with:
// - Connection pooling
// - Prepared statements
// - Async operations
// - Transaction support
// - Result set streaming

#ifndef BEST_SERVER_DATABASE_POSTGRESQL_POSTGRES_CLIENT_HPP
#define BEST_SERVER_DATABASE_POSTGRESQL_POSTGRES_CLIENT_HPP

#include <best_server/future/future.hpp>
#include <best_server/io/tcp_socket.hpp>
#include <string>
#include <vector>
#include <memory>
#include <unordered_map>
#include <variant>

namespace best_server {
namespace database {
namespace postgresql {

// PostgreSQL data types
enum class PostgresType {
    BOOL,
    INT2,
    INT4,
    INT8,
    FLOAT4,
    FLOAT8,
    NUMERIC,
    CHAR,
    VARCHAR,
    TEXT,
    BYTEA,
    DATE,
    TIME,
    TIMESTAMP,
    TIMESTAMPTZ,
    JSON,
    JSONB,
    UUID,
    ARRAY,
    UNKNOWN
};

// PostgreSQL value
using PostgresValue = std::variant<std::monostate,
                                   bool,
                                   int32_t,
                                   int64_t,
                                   double,
                                   std::string,
                                   std::vector<uint8_t>>;

// PostgreSQL result
struct PostgresResult {
    bool success{false};
    std::string error_message;
    uint64_t affected_rows{0};
    std::vector<std::string> column_names;
    std::vector<PostgresType> column_types;
    std::vector<std::vector<PostgresValue>> rows;
    
    explicit operator bool() const { return success; }
    
    // Convenience accessors
    bool is_null(size_t row, size_t col) const;
    bool as_bool(size_t row, size_t col) const;
    int32_t as_int32(size_t row, size_t col) const;
    int64_t as_int64(size_t row, size_t col) const;
    double as_double(size_t row, size_t col) const;
    std::string as_string(size_t row, size_t col) const;
    std::vector<uint8_t> as_blob(size_t row, size_t col) const;
    
    size_t row_count() const { return rows.size(); }
    size_t column_count() const { return column_names.size(); }
};

// PostgreSQL configuration
struct PostgresConfig {
    std::string host{"localhost"};
    uint16_t port{5432};
    std::string username;
    std::string password;
    std::string database;
    std::string application_name{"best_server"};
    uint32_t timeout{5000};
    uint32_t pool_size{10};
    bool enable_ssl{false};
    bool auto_reconnect{true};
};

// PostgreSQL client
class Client {
public:
    using Ptr = std::shared_ptr<Client>;
    
    static future::Future<Ptr> connect(const PostgresConfig& config);
    
    ~Client();
    
    // Execute query
    future::Future<PostgresResult> query(const std::string& sql);
    
    // Execute update/insert/delete
    future::Future<PostgresResult> execute(const std::string& sql);
    
    // Transaction
    future::Future<bool> begin_transaction();
    future::Future<bool> commit();
    future::Future<bool> rollback();
    
    // Connection info
    const PostgresConfig& config() const { return config_; }
    bool is_connected() const { return connected_; }
    
    // Reconnect
    future::Future<bool> reconnect();
    
    // Ping server
    future::Future<bool> ping();
    
private:
    Client(const PostgresConfig& config);
    
    future::Future<bool> connect_internal();
    void disconnect();
    
    PostgresConfig config_;
    std::unique_ptr<io::TCPSocket> socket_;
    bool connected_{false};
    [[maybe_unused]] uint32_t process_id_{0};
    [[maybe_unused]] uint32_t secret_key_{0};
};

// PostgreSQL connection pool
class ConnectionPool {
public:
    using Ptr = std::shared_ptr<ConnectionPool>;
    
    static Ptr create(const PostgresConfig& config);
    
    future::Future<Client::Ptr> acquire();
    void release(Client::Ptr client);
    
    size_t size() const;
    size_t active_count() const;
    size_t idle_count() const;
    
private:
    ConnectionPool(const PostgresConfig& config);
    
    PostgresConfig config_;
    std::vector<Client::Ptr> pool_;
    mutable std::mutex mutex_;
    std::condition_variable cv_;
    std::atomic<size_t> active_count_{0};
};

// PostgreSQL utilities
namespace utils {

// Escape string
std::string escape_string(const std::string& str);

// Build INSERT query
std::string build_insert(const std::string& table, const std::unordered_map<std::string, PostgresValue>& data);

// Build UPDATE query
std::string build_update(const std::string& table, const std::unordered_map<std::string, PostgresValue>& data, const std::string& where);

// Build DELETE query
std::string build_delete(const std::string& table, const std::string& where);

} // namespace utils

} // namespace postgresql
} // namespace database
} // namespace best_server

#endif // BEST_SERVER_DATABASE_POSTGRESQL_POSTGRES_CLIENT_HPP