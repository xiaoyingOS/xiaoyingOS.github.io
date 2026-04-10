// RedisClient implementation
#include "best_server/database/redis/redis_client.hpp"
#include <sstream>
#include <cstring>
#include <arpa/inet.h>
#include <coroutine>
#include <unistd.h>

namespace best_server {
namespace database {
namespace redis {

// RedisResult convenience accessors
std::string RedisResult::as_string() const {
    if (std::holds_alternative<std::string>(value)) {
        return std::get<std::string>(value);
    }
    return "";
}

int64_t RedisResult::as_int() const {
    if (std::holds_alternative<int64_t>(value)) {
        return std::get<int64_t>(value);
    }
    return 0;
}

double RedisResult::as_double() const {
    if (std::holds_alternative<double>(value)) {
        return std::get<double>(value);
    }
    return 0.0;
}

std::vector<RedisValue> RedisResult::as_array() const {
    if (std::holds_alternative<RedisArray>(value)) {
        return std::get<RedisArray>(value).values;
    }
    return {};
}

std::unordered_map<std::string, RedisValue> RedisResult::as_map() const {
    if (std::holds_alternative<RedisMap>(value)) {
        return std::get<RedisMap>(value).entries;
    }
    return {};
}

// Command implementation
Command::Command(const std::string& name)
    : name_(name)
{
}

Command& Command::arg(const std::string& value) {
    args_.push_back(value);
    return *this;
}

Command& Command::arg(int64_t value) {
    args_.push_back(std::to_string(value));
    return *this;
}

Command& Command::arg(double value) {
    args_.push_back(std::to_string(value));
    return *this;
}

Command& Command::arg(const std::vector<std::string>& values) {
    for (const auto& v : values) {
        args_.push_back(v);
    }
    return *this;
}

std::string Command::build() const {
    return utils::serialize_array({name_});
}

// Client implementation
Client::Client(const RedisConfig& config)
    : config_(config)
    , connected_(false)
    , read_buffer_(4096)
{
}

future::Future<Client::Ptr> Client::connect(const RedisConfig& config) {
    return future::make_future<Client::Ptr>([config]() {
        auto client = std::shared_ptr<Client>(new Client(config));
        
        // Create socket
        client->socket_ = std::make_shared<io::TCPSocket>();
        
        // Connect to Redis server using synchronous connection
        if (!client->socket_->connect_sync(config.host, config.port, config.timeout)) {
            return Client::Ptr(nullptr);
        }
        
        client->connected_ = true;
        return client;
    });
}

Client::~Client() {
    if (socket_) {
        socket_->close();
    }
}

future::Future<RedisResult> Client::ping() {
    Command cmd("PING");
    co_return co_await send_command(cmd);
}

future::Future<RedisResult> Client::quit() {
    Command cmd("QUIT");
    auto result = co_await send_command(cmd);
    connected_ = false;
    if (socket_) {
        socket_->close();
    }
    co_return result;
}

future::Future<RedisResult> Client::auth(const std::string& password) {
    Command cmd("AUTH");
    cmd.arg(password);
    co_return co_await send_command(cmd);
}

future::Future<RedisResult> Client::select(uint16_t database) {
    Command cmd("SELECT");
    cmd.arg(static_cast<int64_t>(database));
    co_return co_await send_command(cmd);
}

// String operations
future::Future<RedisResult> Client::get(const std::string& key) {
    Command cmd("GET");
    cmd.arg(key);
    co_return co_await send_command(cmd);
}

future::Future<RedisResult> Client::set(const std::string& key, const std::string& value) {
    Command cmd("SET");
    cmd.arg(key).arg(value);
    co_return co_await send_command(cmd);
}

future::Future<RedisResult> Client::setex(const std::string& key, uint32_t seconds, const std::string& value) {
    Command cmd("SETEX");
    cmd.arg(key).arg(static_cast<int64_t>(seconds)).arg(value);
    co_return co_await send_command(cmd);
}

future::Future<RedisResult> Client::setnx(const std::string& key, const std::string& value) {
    Command cmd("SETNX");
    cmd.arg(key).arg(value);
    co_return co_await send_command(cmd);
}

future::Future<RedisResult> Client::del(const std::string& key) {
    Command cmd("DEL");
    cmd.arg(key);
    co_return co_await send_command(cmd);
}

future::Future<RedisResult> Client::del(const std::vector<std::string>& keys) {
    Command cmd("DEL");
    cmd.arg(keys);
    co_return co_await send_command(cmd);
}

future::Future<RedisResult> Client::exists(const std::string& key) {
    Command cmd("EXISTS");
    cmd.arg(key);
    co_return co_await send_command(cmd);
}

future::Future<RedisResult> Client::expire(const std::string& key, uint32_t seconds) {
    Command cmd("EXPIRE");
    cmd.arg(key).arg(static_cast<int64_t>(seconds));
    co_return co_await send_command(cmd);
}

future::Future<RedisResult> Client::ttl(const std::string& key) {
    Command cmd("TTL");
    cmd.arg(key);
    co_return co_await send_command(cmd);
}

future::Future<RedisResult> Client::incr(const std::string& key) {
    Command cmd("INCR");
    cmd.arg(key);
    co_return co_await send_command(cmd);
}

future::Future<RedisResult> Client::incrby(const std::string& key, int64_t increment) {
    Command cmd("INCRBY");
    cmd.arg(key).arg(increment);
    co_return co_await send_command(cmd);
}

future::Future<RedisResult> Client::decr(const std::string& key) {
    Command cmd("DECR");
    cmd.arg(key);
    co_return co_await send_command(cmd);
}

future::Future<RedisResult> Client::decrby(const std::string& key, int64_t decrement) {
    Command cmd("DECRBY");
    cmd.arg(key).arg(decrement);
    co_return co_await send_command(cmd);
}

// List operations
future::Future<RedisResult> Client::lpush(const std::string& key, const std::string& value) {
    Command cmd("LPUSH");
    cmd.arg(key).arg(value);
    co_return co_await send_command(cmd);
}

future::Future<RedisResult> Client::rpush(const std::string& key, const std::string& value) {
    Command cmd("RPUSH");
    cmd.arg(key).arg(value);
    co_return co_await send_command(cmd);
}

future::Future<RedisResult> Client::lpop(const std::string& key) {
    Command cmd("LPOP");
    cmd.arg(key);
    co_return co_await send_command(cmd);
}

future::Future<RedisResult> Client::rpop(const std::string& key) {
    Command cmd("RPOP");
    cmd.arg(key);
    co_return co_await send_command(cmd);
}

future::Future<RedisResult> Client::lrange(const std::string& key, int64_t start, int64_t stop) {
    Command cmd("LRANGE");
    cmd.arg(key).arg(start).arg(stop);
    co_return co_await send_command(cmd);
}

future::Future<RedisResult> Client::llen(const std::string& key) {
    Command cmd("LLEN");
    cmd.arg(key);
    co_return co_await send_command(cmd);
}

// Set operations
future::Future<RedisResult> Client::sadd(const std::string& key, const std::string& member) {
    Command cmd("SADD");
    cmd.arg(key).arg(member);
    co_return co_await send_command(cmd);
}

future::Future<RedisResult> Client::srem(const std::string& key, const std::string& member) {
    Command cmd("SREM");
    cmd.arg(key).arg(member);
    co_return co_await send_command(cmd);
}

future::Future<RedisResult> Client::smembers(const std::string& key) {
    Command cmd("SMEMBERS");
    cmd.arg(key);
    co_return co_await send_command(cmd);
}

future::Future<RedisResult> Client::sismember(const std::string& key, const std::string& member) {
    Command cmd("SISMEMBER");
    cmd.arg(key).arg(member);
    co_return co_await send_command(cmd);
}

future::Future<RedisResult> Client::scard(const std::string& key) {
    Command cmd("SCARD");
    cmd.arg(key);
    co_return co_await send_command(cmd);
}

// Sorted set operations
future::Future<RedisResult> Client::zadd(const std::string& key, double score, const std::string& member) {
    Command cmd("ZADD");
    cmd.arg(key).arg(score).arg(member);
    co_return co_await send_command(cmd);
}

future::Future<RedisResult> Client::zrem(const std::string& key, const std::string& member) {
    Command cmd("ZREM");
    cmd.arg(key).arg(member);
    co_return co_await send_command(cmd);
}

future::Future<RedisResult> Client::zrange(const std::string& key, int64_t start, int64_t stop) {
    Command cmd("ZRANGE");
    cmd.arg(key).arg(start).arg(stop);
    co_return co_await send_command(cmd);
}

future::Future<RedisResult> Client::zrangebyscore(const std::string& key, double min, double max) {
    Command cmd("ZRANGEBYSCORE");
    cmd.arg(key).arg(min).arg(max);
    co_return co_await send_command(cmd);
}

future::Future<RedisResult> Client::zcard(const std::string& key) {
    Command cmd("ZCARD");
    cmd.arg(key);
    co_return co_await send_command(cmd);
}

future::Future<RedisResult> Client::zscore(const std::string& key, const std::string& member) {
    Command cmd("ZSCORE");
    cmd.arg(key).arg(member);
    co_return co_await send_command(cmd);
}

// Hash operations
future::Future<RedisResult> Client::hset(const std::string& key, const std::string& field, const std::string& value) {
    Command cmd("HSET");
    cmd.arg(key).arg(field).arg(value);
    co_return co_await send_command(cmd);
}

future::Future<RedisResult> Client::hget(const std::string& key, const std::string& field) {
    Command cmd("HGET");
    cmd.arg(key).arg(field);
    co_return co_await send_command(cmd);
}

future::Future<RedisResult> Client::hgetall(const std::string& key) {
    Command cmd("HGETALL");
    cmd.arg(key);
    co_return co_await send_command(cmd);
}

future::Future<RedisResult> Client::hdel(const std::string& key, const std::string& field) {
    Command cmd("HDEL");
    cmd.arg(key).arg(field);
    co_return co_await send_command(cmd);
}

future::Future<RedisResult> Client::hexists(const std::string& key, const std::string& field) {
    Command cmd("HEXISTS");
    cmd.arg(key).arg(field);
    co_return co_await send_command(cmd);
}

future::Future<RedisResult> Client::hkeys(const std::string& key) {
    Command cmd("HKEYS");
    cmd.arg(key);
    co_return co_await send_command(cmd);
}

future::Future<RedisResult> Client::hvals(const std::string& key) {
    Command cmd("HVALS");
    cmd.arg(key);
    co_return co_await send_command(cmd);
}

future::Future<RedisResult> Client::hlen(const std::string& key) {
    Command cmd("HLEN");
    cmd.arg(key);
    co_return co_await send_command(cmd);
}

// Transaction operations
future::Future<RedisResult> Client::multi() {
    Command cmd("MULTI");
    co_return co_await send_command(cmd);
}

future::Future<RedisResult> Client::exec() {
    Command cmd("EXEC");
    co_return co_await send_command(cmd);
}

future::Future<RedisResult> Client::discard() {
    Command cmd("DISCARD");
    co_return co_await send_command(cmd);
}

// Pub/Sub operations
future::Future<RedisResult> Client::publish(const std::string& channel, const std::string& message) {
    Command cmd("PUBLISH");
    cmd.arg(channel).arg(message);
    co_return co_await send_command(cmd);
}

future::Future<RedisResult> Client::subscribe(const std::string& channel) {
    Command cmd("SUBSCRIBE");
    cmd.arg(channel);
    co_return co_await send_command(cmd);
}

future::Future<RedisResult> Client::unsubscribe(const std::string& channel) {
    Command cmd("UNSUBSCRIBE");
    cmd.arg(channel);
    co_return co_await send_command(cmd);
}

// Generic command
future::Future<RedisResult> Client::execute(const Command& command) {
    co_return co_await send_command(command);
}

future::Future<RedisResult> Client::execute(const std::string& command) {
    // Parse command string and build Command object
    // This is a simplified implementation
    std::istringstream iss(command);
    std::string cmd_name;
    iss >> cmd_name;
    
    Command cmd(cmd_name);
    std::string arg;
    while (iss >> arg) {
        cmd.arg(arg);
    }
    
    co_return co_await send_command(cmd);
}

future::Future<RedisResult> Client::send_command(const Command& command) {
    if (!connected_ || !socket_) {
        RedisResult result;
        result.success = false;
        result.error_message = "Not connected to Redis server";
        co_return result;
    }
    
    // Serialize command
    std::string serialized = utils::serialize_array({command.name()});
    for (const auto& arg : command.args()) {
        serialized += utils::serialize_bulk_string(arg);
    }
    
    // Send command
    if (socket_->native_handle() < 0) {
        RedisResult result;
        result.success = false;
        result.error_message = "Not connected to Redis server";
        co_return result;
    }
    
    // Send command using native handle
    ssize_t sent = ::write(socket_->native_handle(), serialized.data(), serialized.size());
    if (sent != static_cast<ssize_t>(serialized.size())) {
        RedisResult result;
        result.success = false;
        result.error_message = "Failed to send command to Redis server";
        co_return result;
    }
    
    // Read response
    std::vector<uint8_t> response_data;
    char buffer[4096];
    
    while (true) {
        ssize_t bytes_read = ::read(socket_->native_handle(), buffer, sizeof(buffer));
        if (bytes_read <= 0) {
            RedisResult result;
            result.success = false;
            result.error_message = "Failed to read response from Redis server";
            co_return result;
        }
        
        response_data.insert(response_data.end(), buffer, buffer + bytes_read);
        
        // Try to parse response
        size_t consumed = 0;
        RedisResult result = parse_response(response_data, consumed);
        
        if (consumed > 0) {
            // Remove consumed data from buffer
            if (consumed < response_data.size()) {
                response_data.erase(response_data.begin(), response_data.begin() + consumed);
            }
            co_return result;
        }
    }
}

RedisResult Client::parse_response(const std::vector<uint8_t>& data, size_t& consumed) {
    if (data.empty()) {
        consumed = 0;
        return RedisResult{};
    }
    
    char type = static_cast<char>(data[0]);
    size_t pos = 1;
    
    RedisResult result;
    result.success = true;
    
    switch (type) {
        case '+': {  // Simple string
            std::string value;
            if (utils::parse_simple_string(data, value, pos)) {
                result.value = value;
                result.type = DataType::String;
                consumed = pos;
            }
            break;
        }
        
        case '-': {  // Error
            std::string error;
            if (utils::parse_error(data, error, pos)) {
                result.success = false;
                result.error_message = error;
                consumed = pos;
            }
            break;
        }
        
        case ':': {  // Integer
            int64_t value;
            if (utils::parse_integer(data, value, pos)) {
                result.value = value;
                result.type = DataType::String;
                consumed = pos;
            }
            break;
        }
        
        case '$': {  // Bulk string
            std::string value;
            if (utils::parse_bulk_string(data, value, pos)) {
                result.value = value;
                result.type = DataType::String;
                consumed = pos;
            }
            break;
        }
        
        case '*': {  // Array
            std::vector<std::string> array;
            if (utils::parse_array(data, array, pos)) {
                std::vector<RedisValue> redis_vec;
                for (const auto& str : array) {
                    redis_vec.push_back(str);
                }
                RedisArray redis_array;
                redis_array.values = std::move(redis_vec);
                result.value = redis_array;
                result.type = DataType::List;
                consumed = pos;
            }
            break;
        }
        
        default:
            consumed = 0;
            break;
    }
    
    return result;
}

// ConnectionPool implementation
ConnectionPool::ConnectionPool(const RedisConfig& config)
    : config_(config)
{
}

ConnectionPool::Ptr ConnectionPool::create(const RedisConfig& config) {
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

std::string serialize_simple_string(const std::string& value) {
    return "+" + value + "\r\n";
}

std::string serialize_error(const std::string& message) {
    return "-" + message + "\r\n";
}

std::string serialize_integer(int64_t value) {
    return ":" + std::to_string(value) + "\r\n";
}

std::string serialize_bulk_string(const std::string& value) {
    return "$" + std::to_string(value.size()) + "\r\n" + value + "\r\n";
}

std::string serialize_null() {
    return "$-1\r\n";
}

std::string serialize_array(const std::vector<std::string>& elements) {
    std::string result = "*" + std::to_string(elements.size()) + "\r\n";
    for (const auto& elem : elements) {
        result += serialize_bulk_string(elem);
    }
    return result;
}

bool parse_simple_string(const std::vector<uint8_t>& data, std::string& result, size_t& consumed) {
    size_t start = consumed;
    while (start < data.size() && data[start] != '\r') {
        start++;
    }
    
    if (start + 1 >= data.size() || data[start + 1] != '\n') {
        return false;
    }
    
    result = std::string(data.begin() + 1, data.begin() + start);
    consumed = start + 2;
    return true;
}

bool parse_error(const std::vector<uint8_t>& data, std::string& result, size_t& consumed) {
    size_t start = consumed;
    while (start < data.size() && data[start] != '\r') {
        start++;
    }
    
    if (start + 1 >= data.size() || data[start + 1] != '\n') {
        return false;
    }
    
    result = std::string(data.begin() + 1, data.begin() + start);
    consumed = start + 2;
    return true;
}

bool parse_integer(const std::vector<uint8_t>& data, int64_t& result, size_t& consumed) {
    size_t start = consumed;
    while (start < data.size() && data[start] != '\r') {
        start++;
    }
    
    if (start + 1 >= data.size() || data[start + 1] != '\n') {
        return false;
    }
    
    result = std::stoll(std::string(data.begin() + 1, data.begin() + start));
    consumed = start + 2;
    return true;
}

bool parse_bulk_string(const std::vector<uint8_t>& data, std::string& result, size_t& consumed) {
    size_t start = consumed;
    while (start < data.size() && data[start] != '\r') {
        start++;
    }
    
    if (start + 1 >= data.size() || data[start + 1] != '\n') {
        return false;
    }
    
    int64_t size = std::stoll(std::string(data.begin() + 1, data.begin() + start));
    
    if (size < 0) {
        // Null value
        consumed = start + 2;
        return true;
    }
    
    if (data.size() < start + 2 + size + 2) {
        return false;  // Incomplete data
    }
    
    result = std::string(data.begin() + start + 2, data.begin() + start + 2 + size);
    consumed = start + 2 + size + 2;
    return true;
}

bool parse_array(const std::vector<uint8_t>& data, std::vector<std::string>& result, size_t& consumed) {
    size_t start = consumed;
    while (start < data.size() && data[start] != '\r') {
        start++;
    }
    
    if (start + 1 >= data.size() || data[start + 1] != '\n') {
        return false;
    }
    
    int64_t count = std::stoll(std::string(data.begin() + 1, data.begin() + start));
    consumed = start + 2;
    
    result.clear();
    for (int64_t i = 0; i < count; i++) {
        std::string element;
        if (!parse_bulk_string(data, element, consumed)) {
            return false;
        }
        result.push_back(element);
    }
    
    return true;
}

std::string generate_key(const std::string& prefix, const std::string& suffix) {
    if (prefix.empty()) {
        return suffix;
    }
    return prefix + ":" + suffix;
}

std::string generate_key(const std::vector<std::string>& parts) {
    if (parts.empty()) {
        return "";
    }
    
    std::string result = parts[0];
    for (size_t i = 1; i < parts.size(); i++) {
        result += ":" + parts[i];
    }
    return result;
}

bool match_pattern(const std::string& pattern, const std::string& key) {
    // Simple glob pattern matching
    // This is a simplified implementation
    size_t p_pos = 0;
    size_t k_pos = 0;
    
    while (p_pos < pattern.size() && k_pos < key.size()) {
        if (pattern[p_pos] == '*') {
            // Match any sequence
            size_t next_pattern = pattern.find('*', p_pos + 1);
            if (next_pattern == std::string::npos) {
                return true;  // Match rest
            }
            
            std::string sub_pattern = pattern.substr(p_pos + 1, next_pattern - p_pos - 1);
            size_t found = key.find(sub_pattern, k_pos);
            if (found == std::string::npos) {
                return false;
            }
            
            p_pos = next_pattern;
            k_pos = found + sub_pattern.size();
        } else if (pattern[p_pos] == '?') {
            // Match any single character
            p_pos++;
            k_pos++;
        } else if (pattern[p_pos] == '[') {
            // Character class
            size_t end = pattern.find(']', p_pos);
            if (end == std::string::npos) {
                return false;
            }
            
            std::string char_class = pattern.substr(p_pos + 1, end - p_pos - 1);
            if (char_class.find(key[k_pos]) == std::string::npos) {
                return false;
            }
            
            p_pos = end + 1;
            k_pos++;
        } else {
            // Exact match
            if (pattern[p_pos] != key[k_pos]) {
                return false;
            }
            p_pos++;
            k_pos++;
        }
    }
    
    // Handle remaining pattern wildcards
    while (p_pos < pattern.size() && pattern[p_pos] == '*') {
        p_pos++;
    }
    
    return p_pos == pattern.size() && k_pos == key.size();
}

} // namespace utils

} // namespace redis
} // namespace database
} // namespace best_server