// ORM - Object-Relational Mapping framework
// 
// Provides a type-safe ORM with:
// - Model definitions
// - Query builder
// - Relationships
// - Migrations
// - Validation

#ifndef BEST_SERVER_DATABASE_ORM_ORM_HPP
#define BEST_SERVER_DATABASE_ORM_ORM_HPP

#include <string>
#include <vector>
#include <memory>
#include <unordered_map>
#include <functional>
#include <optional>
#include <type_traits>
#include <chrono>

namespace best_server {
namespace database {
namespace orm {

// Forward declarations
class Database;
class QueryBuilder;

// Field types
enum class FieldType {
    INTEGER,
    BIGINT,
    FLOAT,
    DOUBLE,
    STRING,
    TEXT,
    BOOLEAN,
    DATE,
    DATETIME,
    TIMESTAMP,
    JSON,
    BLOB,
    REFERENCE
};

// Field definition
struct Field {
    std::string name;
    FieldType type;
    bool primary_key{false};
    bool auto_increment{false};
    bool nullable{true};
    bool unique{false};
    bool indexed{false};
    size_t string_length{255};
    std::string default_value;
    std::string reference_table;
    std::string reference_field;
};

// Model base class
class Model {
public:
    virtual ~Model() = default;
    
    // Get table name
    virtual std::string table_name() const = 0;
    
    // Get fields
    virtual std::vector<Field> fields() const = 0;
    
    // Get primary key value
    virtual int64_t id() const = 0;
    virtual void set_id(int64_t id) = 0;
    
    // Validate
    virtual bool validate([[maybe_unused]] std::vector<std::string>& errors) const { return true; }
    
    // Serialize to field values
    virtual std::unordered_map<std::string, std::string> to_values() const = 0;
    
    // Deserialize from field values
    virtual void from_values(const std::unordered_map<std::string, std::string>& values) = 0;
    
    // Timestamps
    std::chrono::system_clock::time_point created_at() const { return created_at_; }
    std::chrono::system_clock::time_point updated_at() const { return updated_at_; }
    void set_created_at(const std::chrono::system_clock::time_point& time) { created_at_ = time; }
    void set_updated_at(const std::chrono::system_clock::time_point& time) { updated_at_ = time; }
    
protected:
    std::chrono::system_clock::time_point created_at_;
    std::chrono::system_clock::time_point updated_at_;
};

// Query result wrapper
template<typename T>
class QueryResult {
public:
    using Ptr = std::shared_ptr<QueryResult<T>>;
    
    QueryResult() = default;
    
    bool success() const { return success_; }
    const std::string& error() const { return error_; }
    
    const std::vector<std::shared_ptr<T>>& results() const { return results_; }
    size_t count() const { return results_.size(); }
    
    bool empty() const { return results_.empty(); }
    
    std::shared_ptr<T> first() const {
        return results_.empty() ? nullptr : results_[0];
    }
    
private:
    bool success_{true};
    std::string error_;
    std::vector<std::shared_ptr<T>> results_;
    
    friend class Database;
};

// Database interface
class Database {
public:
    using Ptr = std::shared_ptr<Database>;
    
    virtual ~Database() = default;
    
    // Create table
    virtual bool create_table(const Model& model) = 0;
    
    // Drop table
    virtual bool drop_table(const std::string& table_name) = 0;
    
    // Insert
    template<typename T>
    bool insert(std::shared_ptr<T> model) {
        auto values = model->to_values();
        std::string sql = build_insert<T>(values);
        return execute_sql(sql);
    }
    
    // Update
    template<typename T>
    bool update(std::shared_ptr<T> model) {
        auto values = model->to_values();
        std::string where = "id = " + std::to_string(model->id());
        std::string sql = build_update<T>(values, where);
        return execute_sql(sql);
    }
    
    // Delete
    template<typename T>
    bool delete_by_id(int64_t id) {
        std::string table_name = T().table_name();
        std::string sql = "DELETE FROM " + table_name + " WHERE id = " + std::to_string(id);
        return execute_sql(sql);
    }
    
    // Find by ID
    template<typename T>
    std::shared_ptr<T> find_by_id(int64_t id) {
        std::string table_name = T().table_name();
        std::string sql = "SELECT * FROM " + table_name + " WHERE id = " + std::to_string(id);
        
        auto result = query_sql(sql);
        if (!result.success || result.rows.empty()) {
            return nullptr;
        }
        
        auto model = std::make_shared<T>();
        model->from_values(result.rows[0]);
        return model;
    }
    
    // Find all
    template<typename T>
    std::vector<std::shared_ptr<T>> find_all() {
        std::string table_name = T().table_name();
        std::string sql = "SELECT * FROM " + table_name;
        
        auto result = query_sql(sql);
        if (!result.success) {
            return {};
        }
        
        std::vector<std::shared_ptr<T>> models;
        for (const auto& row : result.rows) {
            auto model = std::make_shared<T>();
            model->from_values(row);
            models.push_back(model);
        }
        
        return models;
    }
    
    // Find by condition
    template<typename T>
    std::vector<std::shared_ptr<T>> find_by(const std::string& condition) {
        std::string table_name = T().table_name();
        std::string sql = "SELECT * FROM " + table_name + " WHERE " + condition;
        
        auto result = query_sql(sql);
        if (!result.success) {
            return {};
        }
        
        std::vector<std::shared_ptr<T>> models;
        for (const auto& row : result.rows) {
            auto model = std::make_shared<T>();
            model->from_values(row);
            models.push_back(model);
        }
        
        return models;
    }
    
    // Count
    template<typename T>
    size_t count() {
        std::string table_name = T().table_name();
        std::string sql = "SELECT COUNT(*) FROM " + table_name;
        
        auto result = query_sql(sql);
        if (!result.success || result.rows.empty()) {
            return 0;
        }
        
        return std::stoull(result.rows[0]["count"]);
    }
    
    // Transaction
    virtual bool begin_transaction() = 0;
    virtual bool commit() = 0;
    virtual bool rollback() = 0;
    
protected:
    struct QueryResult {
        bool success;
        std::string error;
        std::vector<std::unordered_map<std::string, std::string>> rows;
    };
    
    virtual QueryResult query_sql(const std::string& sql) = 0;
    virtual bool execute_sql(const std::string& sql) = 0;
    
    template<typename T>
    std::string build_insert(const std::unordered_map<std::string, std::string>& values) {
        T model;
        std::string table_name = model.table_name();
        
        std::ostringstream oss;
        oss << "INSERT INTO " << table_name << " (";
        
        bool first = true;
        for (const auto& [key, _] : values) {
            if (key == "id") continue;  // Skip auto-increment ID
            if (!first) oss << ", ";
            oss << key;
            first = false;
        }
        
        oss << ") VALUES (";
        
        first = true;
        for (const auto& [key, value] : values) {
            if (key == "id") continue;
            if (!first) oss << ", ";
            oss << "'" << value << "'";
            first = false;
        }
        
        oss << ")";
        
        return oss.str();
    }
    
    template<typename T>
    std::string build_update(const std::unordered_map<std::string, std::string>& values, const std::string& where) {
        T model;
        std::string table_name = model.table_name();
        
        std::ostringstream oss;
        oss << "UPDATE " << table_name << " SET ";
        
        bool first = true;
        for (const auto& [key, value] : values) {
            if (key == "id") continue;
            if (!first) oss << ", ";
            oss << key << " = '" << value << "'";
            first = false;
        }
        
        oss << " WHERE " << where;
        
        return oss.str();
    }
};

// Migration
class Migration {
public:
    virtual ~Migration() = default;
    
    virtual std::string name() const = 0;
    virtual std::string version() const = 0;
    
    virtual bool up(Database& db) = 0;
    virtual bool down(Database& db) = 0;
};

// Migration runner
class MigrationRunner {
public:
    using Ptr = std::shared_ptr<MigrationRunner>;
    
    static Ptr create(Database::Ptr db) {
        return std::make_shared<MigrationRunner>(db);
    }
    
    MigrationRunner(Database::Ptr db) : db_(db) {}
    
    void add_migration(std::shared_ptr<Migration> migration) {
        migrations_.push_back(migration);
    }
    
    bool migrate() {
        // This would track migration status
        for (auto& migration : migrations_) {
            if (!migration->up(*db_)) {
                return false;
            }
        }
        return true;
    }
    
    bool rollback() {
        for (auto it = migrations_.rbegin(); it != migrations_.rend(); ++it) {
            if (!(*it)->down(*db_)) {
                return false;
            }
        }
        return true;
    }
    
private:
    Database::Ptr db_;
    std::vector<std::shared_ptr<Migration>> migrations_;
};

// Validation utilities
namespace validation {

struct ValidationError {
    std::string field;
    std::string message;
};

std::vector<ValidationError> validate_required(const std::string& value, const std::string& field_name);
std::vector<ValidationError> validate_length(const std::string& value, size_t min, size_t max, const std::string& field_name);
std::vector<ValidationError> validate_email(const std::string& value, const std::string& field_name);
std::vector<ValidationError> validate_pattern(const std::string& value, const std::string& pattern, const std::string& field_name);
std::vector<ValidationError> validate_range(int64_t value, int64_t min, int64_t max, const std::string& field_name);

} // namespace validation

} // namespace orm
} // namespace database
} // namespace best_server

#endif // BEST_SERVER_DATABASE_ORM_ORM_HPP