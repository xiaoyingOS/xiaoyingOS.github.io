// PostgreSQLClient implementation
#include "best_server/database/postgresql/postgres_client.hpp"
#include <sstream>
#include <cstring>

namespace best_server {
namespace database {
namespace postgresql {

// PostgresResult convenience accessors
bool PostgresResult::is_null(size_t row, size_t col) const {
    if (row >= rows.size() || col >= rows[row].size()) {
        return true;
    }
    return std::holds_alternative<std::monostate>(rows[row][col]);
}

bool PostgresResult::as_bool(size_t row, size_t col) const {
    if (row >= rows.size() || col >= rows[row].size()) {
        return false;
    }
    if (std::holds_alternative<bool>(rows[row][col])) {
        return std::get<bool>(rows[row][col]);
    }
    if (std::holds_alternative<int32_t>(rows[row][col])) {
        return std::get<int32_t>(rows[row][col]) != 0;
    }
    return false;
}

int32_t PostgresResult::as_int32(size_t row, size_t col) const {
    if (row >= rows.size() || col >= rows[row].size()) {
        return 0;
    }
    if (std::holds_alternative<int32_t>(rows[row][col])) {
        return std::get<int32_t>(rows[row][col]);
    }
    if (std::holds_alternative<int64_t>(rows[row][col])) {
        return static_cast<int32_t>(std::get<int64_t>(rows[row][col]));
    }
    return 0;
}

int64_t PostgresResult::as_int64(size_t row, size_t col) const {
    if (row >= rows.size() || col >= rows[row].size()) {
        return 0;
    }
    if (std::holds_alternative<int64_t>(rows[row][col])) {
        return std::get<int64_t>(rows[row][col]);
    }
    if (std::holds_alternative<int32_t>(rows[row][col])) {
        return std::get<int32_t>(rows[row][col]);
    }
    return 0;
}

double PostgresResult::as_double(size_t row, size_t col) const {
    if (row >= rows.size() || col >= rows[row].size()) {
        return 0.0;
    }
    if (std::holds_alternative<double>(rows[row][col])) {
        return std::get<double>(rows[row][col]);
    }
    return 0.0;
}

std::string PostgresResult::as_string(size_t row, size_t col) const {
    if (row >= rows.size() || col >= rows[row].size()) {
        return "";
    }
    if (std::holds_alternative<std::string>(rows[row][col])) {
        return std::get<std::string>(rows[row][col]);
    }
    if (std::holds_alternative<int64_t>(rows[row][col])) {
        return std::to_string(std::get<int64_t>(rows[row][col]));
    }
    if (std::holds_alternative<double>(rows[row][col])) {
        return std::to_string(std::get<double>(rows[row][col]));
    }
    return "";
}

std::vector<uint8_t> PostgresResult::as_blob(size_t row, size_t col) const {
    if (row >= rows.size() || col >= rows[row].size()) {
        return {};
    }
    if (std::holds_alternative<std::vector<uint8_t>>(rows[row][col])) {
        return std::get<std::vector<uint8_t>>(rows[row][col]);
    }
    return {};
}

// Client implementation
Client::Client(const PostgresConfig& config)
    : config_(config)
    , connected_(false)
{
}

future::Future<Client::Ptr> Client::connect(const PostgresConfig& config) {
    auto client = std::shared_ptr<Client>(new Client(config));
    
    auto success = co_await client->connect_internal();
    if (!success) {
        co_return nullptr;
    }
    
    co_return client;
}

future::Future<bool> Client::connect_internal() {
    // Create socket
    socket_ = std::make_unique<io::TCPSocket>();
    
    // Connect to PostgreSQL server
    if (!socket_->connect_sync(config_.host, config_.port, config_.timeout)) {
        co_return false;
    }
    
    // This is a simplified implementation
    // In a real implementation, you would perform the PostgreSQL startup protocol
    
    connected_ = true;
    co_return true;
}

void Client::disconnect() {
    if (socket_) {
        socket_->close();
        socket_.reset();
    }
    connected_ = false;
}

Client::~Client() {
    disconnect();
}

future::Future<PostgresResult> Client::query([[maybe_unused]] const std::string& sql) {
    PostgresResult result;
    result.success = true;
    
    // This is a simplified implementation
    // In a real implementation, you would send the query and parse the response
    
    co_return result;
}

future::Future<PostgresResult> Client::execute([[maybe_unused]] const std::string& sql) {
    PostgresResult result;
    result.success = true;
    
    // This is a simplified implementation
    // In a real implementation, you would send the query and parse the response
    
    co_return result;
}

future::Future<bool> Client::begin_transaction() {
    auto result = co_await execute("BEGIN");
    co_return result.success;
}

future::Future<bool> Client::commit() {
    auto result = co_await execute("COMMIT");
    co_return result.success;
}

future::Future<bool> Client::rollback() {
    auto result = co_await execute("ROLLBACK");
    co_return result.success;
}

future::Future<bool> Client::reconnect() {
    disconnect();
    co_return co_await connect_internal();
}

future::Future<bool> Client::ping() {
    if (!connected_ || !socket_) {
        co_return false;
    }
    
    // This would send a PostgreSQL ping
    co_return true;
}

// ConnectionPool implementation
ConnectionPool::ConnectionPool(const PostgresConfig& config)
    : config_(config)
{
}

ConnectionPool::Ptr ConnectionPool::create(const PostgresConfig& config) {
    return std::shared_ptr<ConnectionPool>(new ConnectionPool(config));
}

future::Future<Client::Ptr> ConnectionPool::acquire() {
    std::unique_lock<std::mutex> lock(mutex_);
    
    // Wait for available connection
    cv_.wait(lock, [this]() {
        return !pool_.empty() || active_count_.load() < config_.pool_size;
    });
    
    Client::Ptr client;
    
    if (!pool_.empty()) {
        client = pool_.back();
        pool_.pop_back();
    } else {
        // Create new connection
        lock.unlock();
        client = co_await Client::connect(config_);
        if (!client) {
            co_return nullptr;
        }
        lock.lock();
    }
    
    active_count_++;
    co_return client;
}

void ConnectionPool::release(Client::Ptr client) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (client && client->is_connected()) {
        pool_.push_back(client);
    }
    
    active_count_--;
    cv_.notify_one();
}

size_t ConnectionPool::size() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return pool_.size();
}

size_t ConnectionPool::active_count() const {
    return active_count_.load();
}

size_t ConnectionPool::idle_count() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return pool_.size();
}

// Utilities implementation
namespace utils {

std::string escape_string(const std::string& str) {
    std::string result;
    result.reserve(str.size() * 2);
    
    for (char c : str) {
        switch (c) {
            case '\'':
                result += "''";
                break;
            case '\\':
                result += "\\\\";
                break;
            case '\0':
                result += "\\0";
                break;
            case '\n':
                result += "\\n";
                break;
            case '\r':
                result += "\\r";
                break;
            case '\t':
                result += "\\t";
                break;
            default:
                result += c;
                break;
        }
    }
    
    return result;
}

std::string build_insert(const std::string& table, const std::unordered_map<std::string, PostgresValue>& data) {
    std::ostringstream oss;
    
    oss << "INSERT INTO " << table << " (";
    
    // Columns
    bool first = true;
    for (const auto& [column, value] : data) {
        if (!first) {
            oss << ", ";
        }
        oss << column;
        first = false;
    }
    
    oss << ") VALUES (";
    
    // Values
    first = true;
    for (const auto& [column, value] : data) {
        if (!first) {
            oss << ", ";
        }
        
        if (std::holds_alternative<std::monostate>(value)) {
            oss << "NULL";
        } else if (std::holds_alternative<bool>(value)) {
            oss << (std::get<bool>(value) ? "TRUE" : "FALSE");
        } else if (std::holds_alternative<int32_t>(value)) {
            oss << std::get<int32_t>(value);
        } else if (std::holds_alternative<int64_t>(value)) {
            oss << std::get<int64_t>(value);
        } else if (std::holds_alternative<double>(value)) {
            oss << std::get<double>(value);
        } else if (std::holds_alternative<std::string>(value)) {
            oss << "'" << escape_string(std::get<std::string>(value)) << "'";
        } else if (std::holds_alternative<std::vector<uint8_t>>(value)) {
            oss << "'\\x";
            const auto& blob = std::get<std::vector<uint8_t>>(value);
            for (uint8_t byte : blob) {
                char buf[3];
                sprintf(buf, "%02x", byte);
                oss << buf;
            }
            oss << "'";
        }
        
        first = false;
    }
    
    oss << ")";
    
    return oss.str();
}

std::string build_update(const std::string& table, const std::unordered_map<std::string, PostgresValue>& data, const std::string& where) {
    std::ostringstream oss;
    
    oss << "UPDATE " << table << " SET ";
    
    // Set clauses
    bool first = true;
    for (const auto& [column, value] : data) {
        if (!first) {
            oss << ", ";
        }
        
        oss << column << " = ";
        
        if (std::holds_alternative<std::monostate>(value)) {
            oss << "NULL";
        } else if (std::holds_alternative<bool>(value)) {
            oss << (std::get<bool>(value) ? "TRUE" : "FALSE");
        } else if (std::holds_alternative<int32_t>(value)) {
            oss << std::get<int32_t>(value);
        } else if (std::holds_alternative<int64_t>(value)) {
            oss << std::get<int64_t>(value);
        } else if (std::holds_alternative<double>(value)) {
            oss << std::get<double>(value);
        } else if (std::holds_alternative<std::string>(value)) {
            oss << "'" << escape_string(std::get<std::string>(value)) << "'";
        } else if (std::holds_alternative<std::vector<uint8_t>>(value)) {
            oss << "'\\x";
            const auto& blob = std::get<std::vector<uint8_t>>(value);
            for (uint8_t byte : blob) {
                char buf[3];
                sprintf(buf, "%02x", byte);
                oss << buf;
            }
            oss << "'";
        }
        
        first = false;
    }
    
    // WHERE clause
    if (!where.empty()) {
        oss << " WHERE " << where;
    }
    
    return oss.str();
}

std::string build_delete(const std::string& table, const std::string& where) {
    std::ostringstream oss;
    
    oss << "DELETE FROM " << table;
    
    if (!where.empty()) {
        oss << " WHERE " << where;
    }
    
    return oss.str();
}

} // namespace utils

} // namespace postgresql
} // namespace database
} // namespace best_server