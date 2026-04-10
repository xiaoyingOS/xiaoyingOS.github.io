#pragma once

#include <best_server/future/future.hpp>
#include <string>
#include <unordered_map>
#include <functional>
#include <memory>
#include <variant>

namespace best_server {
namespace config {

// Configuration value type
using ConfigValue = std::variant<std::monostate,
                                std::string,
                                int64_t,
                                double,
                                bool,
                                std::vector<std::string>>;

// Configuration change callback
using ChangeCallback = std::function<void(const std::string& key, const ConfigValue& value)>;

// Configuration manager
class Manager {
public:
    using Ptr = std::shared_ptr<Manager>;
    
    static Ptr create();
    
    // Load configuration
    future::Future<bool> load_from_file(const std::string& file_path);
    future::Future<bool> load_from_env();
    future::Future<bool> load_from_string(const std::string& content);
    
    // Save configuration
    future::Future<bool> save_to_file(const std::string& file_path);
    
    // Get configuration values
    ConfigValue get(const std::string& key) const;
    std::string get_string(const std::string& key, const std::string& default_val = "") const;
    int64_t get_int(const std::string& key, int64_t default_val = 0) const;
    double get_double(const std::string& key, double default_val = 0.0) const;
    bool get_bool(const std::string& key, bool default_val = false) const;
    std::vector<std::string> get_string_array(const std::string& key) const;
    
    // Set configuration values
    void set(const std::string& key, const ConfigValue& value);
    void set_string(const std::string& key, const std::string& value);
    void set_int(const std::string& key, int64_t value);
    void set_double(const std::string& key, double value);
    void set_bool(const std::string& key, bool value);
    void set_string_array(const std::string& key, const std::vector<std::string>& value);
    
    // Check if key exists
    bool has(const std::string& key) const;
    
    // Remove configuration
    void remove(const std::string& key);
    
    // Clear all configuration
    void clear();
    
    // Watch for changes
    void watch(const std::string& key, ChangeCallback callback);
    void unwatch(const std::string& key);
    
    // Get all keys
    std::vector<std::string> keys() const;
    
    // Get all configuration
    std::unordered_map<std::string, ConfigValue> all() const;
    
private:
    Manager();
    
    std::unordered_map<std::string, ConfigValue> config_;
    std::unordered_map<std::string, std::vector<ChangeCallback>> watchers_;
    mutable std::mutex mutex_;
};

// Global configuration manager
Manager& global_config();

} // namespace config
} // namespace best_server