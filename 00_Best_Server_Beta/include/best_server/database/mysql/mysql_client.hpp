// MySQL Client - High-performance MySQL database client
// 
// Implements a MySQL client with:
// - Connection pooling
// - Prepared statements
// - Async operations
// - Transaction support
// - Result set streaming

#ifndef BEST_SERVER_DATABASE_MYSQL_MYSQL_CLIENT_HPP
#define BEST_SERVER_DATABASE_MYSQL_MYSQL_CLIENT_HPP

#include <best_server/future/future.hpp>
#include <best_server/io/tcp_socket.hpp>
#include <string>
#include <vector>
#include <memory>
#include <unordered_map>
#include <variant>

namespace best_server {
namespace database {
namespace mysql {

// MySQL data types
enum class MySQLType {
    TINYINT,
    SMALLINT,
    MEDIUMINT,
    INT,
    BIGINT,
    FLOAT,
    DOUBLE,
    DECIMAL,
    DATE,
    TIME,
    DATETIME,
    TIMESTAMP,
    YEAR,
    CHAR,
    VARCHAR,
    BINARY,
    VARBINARY,
    TINYBLOB,
    BLOB,
    MEDIUMBLOB,
    LONGBLOB,
    TINYTEXT,
    TEXT,
    MEDIUMTEXT,
    LONGTEXT,
    ENUM,
    SET,
    JSON,
    GEOMETRY,
    UNKNOWN
};

// MySQL value
using MySQLValue = std::variant<std::monostate,
                                bool,
                                int32_t,
                                int64_t,
                                double,
                                std::string,
                                std::vector<uint8_t>>;

// MySQL result
struct MySQLResult {
    bool success{false};
    std::string error_message;
    uint64_t last_insert_id{0};
    uint64_t affected_rows{0};
    std::vector<std::string> column_names;
    std::vector<MySQLType> column_types;
    std::vector<std::vector<MySQLValue>> rows;
    
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

// MySQL configuration
struct MySQLConfig {
    std::string host{"localhost"};
    uint16_t port{3306};
    std::string username;
    std::string password;
    std::string database;
    std::string charset{"utf8mb4"};
    uint32_t timeout{5000};
    uint32_t pool_size{10};
    bool enable_ssl{false};
    bool auto_reconnect{true};
    uint32_t max_reconnect_attempts{3};
};

// MySQL prepared statement
class PreparedStatement {
public:
    using Ptr = std::shared_ptr<PreparedStatement>;
    
    ~PreparedStatement();
    
    // Bind parameters
    void bind_param(size_t index, const MySQLValue& value);
    void bind_param(size_t index, bool value);
    void bind_param(size_t index, int32_t value);
    void bind_param(size_t index, int64_t value);
    void bind_param(size_t index, double value);
    void bind_param(size_t index, const std::string& value);
    void bind_param(size_t index, const std::vector<uint8_t>& value);
    void bind_null(size_t index);
    
    // Execute query
    future::Future<MySQLResult> execute();
    
    // Get parameter count
    size_t param_count() const { return param_count_; }
    
    // Clear parameters
    void clear_params();
    
private:
    PreparedStatement(const std::string& query, size_t param_count);
    
    std::string query_;
    size_t param_count_;
    std::vector<MySQLValue> params_;
    
    friend class Client;
};

// MySQL client
class Client {
public:
    using Ptr = std::shared_ptr<Client>;
    
    static future::Future<Ptr> connect(const MySQLConfig& config);
    
    ~Client();
    
    // Execute query
    future::Future<MySQLResult> query(const std::string& sql);
    
    // Execute update/insert/delete
    future::Future<MySQLResult> execute(const std::string& sql);
    
    // Prepare statement
    future::Future<PreparedStatement::Ptr> prepare(const std::string& sql);
    
    // Transaction
    future::Future<bool> begin_transaction();
    future::Future<bool> commit();
    future::Future<bool> rollback();
    
    // Connection info
    const MySQLConfig& config() const { return config_; }
    bool is_connected() const { return connected_; }
    
    // Reconnect
    future::Future<bool> reconnect();
    
    // Ping server
    future::Future<bool> ping();
    
private:
    Client(const MySQLConfig& config);
    
    future::Future<bool> connect_internal();
    void disconnect();
    
    MySQLConfig config_;
    std::unique_ptr<io::TCPSocket> socket_;
    bool connected_{false};
    [[maybe_unused]] uint32_t connection_id_{0};
    std::string server_version_;
    
    // Protocol version
    [[maybe_unused]] uint8_t protocol_version_{10};
};

// MySQL connection pool
class ConnectionPool {
public:
    using Ptr = std::shared_ptr<ConnectionPool>;
    
    static Ptr create(const MySQLConfig& config);
    
    future::Future<Client::Ptr> acquire();
    void release(Client::Ptr client);
    
    size_t size() const;
    size_t active_count() const;
    size_t idle_count() const;
    
private:
    ConnectionPool(const MySQLConfig& config);
    
    MySQLConfig config_;
    std::vector<Client::Ptr> pool_;
    mutable std::mutex mutex_;
    std::condition_variable cv_;
    std::atomic<size_t> active_count_{0};
};

// MySQL query builder
class QueryBuilder {
public:
    QueryBuilder();
    
    QueryBuilder& select(const std::vector<std::string>& columns);
    QueryBuilder& from(const std::string& table);
    QueryBuilder& where(const std::string& condition);
    QueryBuilder& and_where(const std::string& condition);
    QueryBuilder& or_where(const std::string& condition);
    QueryBuilder& join(const std::string& table, const std::string& on);
    QueryBuilder& left_join(const std::string& table, const std::string& on);
    QueryBuilder& right_join(const std::string& table, const std::string& on);
    QueryBuilder& group_by(const std::vector<std::string>& columns);
    QueryBuilder& having(const std::string& condition);
    QueryBuilder& order_by(const std::string& column, bool ascending = true);
    QueryBuilder& limit(uint64_t limit);
    QueryBuilder& offset(uint64_t offset);
    
    std::string build() const;
    
private:
    std::vector<std::string> select_columns_;
    std::string from_table_;
    std::vector<std::string> where_conditions_;
    std::vector<std::string> joins_;
    std::vector<std::string> group_by_columns_;
    std::string having_condition_;
    std::vector<std::pair<std::string, bool>> order_by_columns_;
    uint64_t limit_{0};
    uint64_t offset_{0};
};

// MySQL utilities
namespace utils {

// Escape string
std::string escape_string(const std::string& str);

// Build INSERT query
std::string build_insert(const std::string& table, const std::unordered_map<std::string, MySQLValue>& data);

// Build UPDATE query
std::string build_update(const std::string& table, const std::unordered_map<std::string, MySQLValue>& data, const std::string& where);

// Build DELETE query
std::string build_delete(const std::string& table, const std::string& where);

// Convert MySQL type to string
std::string type_to_string(MySQLType type);

// Parse MySQL type from string
MySQLType type_from_string(const std::string& str);

} // namespace utils

} // namespace mysql
} // namespace database
} // namespace best_server

#endif // BEST_SERVER_DATABASE_MYSQL_MYSQL_CLIENT_HPP