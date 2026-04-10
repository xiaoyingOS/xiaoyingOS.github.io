// ConfigManager implementation
#include "best_server/config/config_manager.hpp"
#include <fstream>
#include <sstream>
#include <algorithm>
#include <regex>

namespace best_server {
namespace config {

// Manager implementation
Manager::Ptr Manager::create() {
    return std::shared_ptr<Manager>(new Manager());
}

Manager::Manager() {
}

future::Future<bool> Manager::load_from_file(const std::string& file_path) {
    std::ifstream file(file_path);
    if (!file.is_open()) {
        return future::make_ready_future<bool>(false);
    }

    std::string content((std::istreambuf_iterator<char>(file)),
                      std::istreambuf_iterator<char>());

    return load_from_string(content);
}

future::Future<bool> Manager::load_from_env() {
    // In a real implementation, you would load environment variables
    // For now, just return true
    return future::make_ready_future<bool>(true);
}

future::Future<bool> Manager::load_from_string(const std::string& content) {
    std::lock_guard<std::mutex> lock(mutex_);
        
        // Simple key-value parser (format: key = value)
    std::istringstream iss(content);
    std::string line;

    while (std::getline(iss, line)) {
        // Skip comments and empty lines
        if (line.empty() || line[0] == '#') {
            continue;
        }

        // Parse key-value
        size_t pos = line.find('=');
        if (pos != std::string::npos) {
            std::string key = line.substr(0, pos);
            std::string value = line.substr(pos + 1);

            // Trim whitespace
            key.erase(0, key.find_first_not_of(" \t"));
            key.erase(key.find_last_not_of(" \t") + 1);
            value.erase(0, value.find_first_not_of(" \t"));
            value.erase(value.find_last_not_of(" \t") + 1);

            // Remove quotes
            if (value.size() >= 2 &&
                ((value[0] == '"' && value.back() == '"') ||
                 (value[0] == '\'' && value.back() == '\''))) {
                value = value.substr(1, value.size() - 2);
            }

            // Determine value type
            if (value == "true") {
                config_[key] = true;
            } else if (value == "false") {
                config_[key] = false;
            } else if (value.find('.') != std::string::npos) {
                try {
                    config_[key] = std::stod(value);
                } catch (...) {
                    config_[key] = value;
                }
            } else {
                try {
                    config_[key] = std::stoll(value);
                } catch (...) {
                    config_[key] = value;
                }
            }
        }
    }

    return future::make_ready_future<bool>(true);
}

future::Future<bool> Manager::save_to_file(const std::string& file_path) {
        std::ofstream file(file_path);
    if (!file.is_open()) {
        return future::make_ready_future<bool>(false);
    }

    std::lock_guard<std::mutex> lock(mutex_);

    for (const auto& [key, value] : config_) {
        file << key << " = ";

        if (std::holds_alternative<std::string>(value)) {
            file << "\"" << std::get<std::string>(value) << "\"";
        } else if (std::holds_alternative<int64_t>(value)) {
            file << std::get<int64_t>(value);
        } else if (std::holds_alternative<double>(value)) {
            file << std::get<double>(value);
        } else if (std::holds_alternative<bool>(value)) {
            file << (std::get<bool>(value) ? "true" : "false");
        }

        file << "\n";
    }

    return future::make_ready_future<bool>(true);
}

ConfigValue Manager::get(const std::string& key) const {
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto it = config_.find(key);
    if (it != config_.end()) {
        return it->second;
    }
    
    return ConfigValue{};
}

std::string Manager::get_string(const std::string& key, const std::string& default_val) const {
    auto value = get(key);
    if (std::holds_alternative<std::string>(value)) {
        return std::get<std::string>(value);
    } else if (std::holds_alternative<int64_t>(value)) {
        return std::to_string(std::get<int64_t>(value));
    } else if (std::holds_alternative<double>(value)) {
        return std::to_string(std::get<double>(value));
    } else if (std::holds_alternative<bool>(value)) {
        return std::get<bool>(value) ? "true" : "false";
    }
    
    return default_val;
}

int64_t Manager::get_int(const std::string& key, int64_t default_val) const {
    auto value = get(key);
    if (std::holds_alternative<int64_t>(value)) {
        return std::get<int64_t>(value);
    } else if (std::holds_alternative<std::string>(value)) {
        try {
            return std::stoll(std::get<std::string>(value));
        } catch (...) {
            return default_val;
        }
    } else if (std::holds_alternative<double>(value)) {
        return static_cast<int64_t>(std::get<double>(value));
    } else if (std::holds_alternative<bool>(value)) {
        return std::get<bool>(value) ? 1 : 0;
    }
    
    return default_val;
}

double Manager::get_double(const std::string& key, double default_val) const {
    auto value = get(key);
    if (std::holds_alternative<double>(value)) {
        return std::get<double>(value);
    } else if (std::holds_alternative<int64_t>(value)) {
        return static_cast<double>(std::get<int64_t>(value));
    } else if (std::holds_alternative<std::string>(value)) {
        try {
            return std::stod(std::get<std::string>(value));
        } catch (...) {
            return default_val;
        }
    } else if (std::holds_alternative<bool>(value)) {
        return std::get<bool>(value) ? 1.0 : 0.0;
    }
    
    return default_val;
}

bool Manager::get_bool(const std::string& key, bool default_val) const {
    auto value = get(key);
    if (std::holds_alternative<bool>(value)) {
        return std::get<bool>(value);
    } else if (std::holds_alternative<std::string>(value)) {
        std::string str = std::get<std::string>(value);
        std::transform(str.begin(), str.end(), str.begin(), ::tolower);
        return str == "true" || str == "1" || str == "yes" || str == "on";
    } else if (std::holds_alternative<int64_t>(value)) {
        return std::get<int64_t>(value) != 0;
    } else if (std::holds_alternative<double>(value)) {
        return std::get<double>(value) != 0.0;
    }
    
    return default_val;
}

std::vector<std::string> Manager::get_string_array(const std::string& key) const {
    auto value = get(key);
    if (std::holds_alternative<std::vector<std::string>>(value)) {
        return std::get<std::vector<std::string>>(value);
    }
    
    return std::vector<std::string>{};
}

void Manager::set(const std::string& key, const ConfigValue& value) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    config_[key] = value;
    
    // Notify watchers
    auto it = watchers_.find(key);
    if (it != watchers_.end()) {
        for (auto& callback : it->second) {
            callback(key, value);
        }
    }
}

void Manager::set_string(const std::string& key, const std::string& value) {
    set(key, value);
}

void Manager::set_int(const std::string& key, int64_t value) {
    set(key, value);
}

void Manager::set_double(const std::string& key, double value) {
    set(key, value);
}

void Manager::set_bool(const std::string& key, bool value) {
    set(key, value);
}

void Manager::set_string_array(const std::string& key, const std::vector<std::string>& value) {
    set(key, value);
}

bool Manager::has(const std::string& key) const {
    std::lock_guard<std::mutex> lock(mutex_);
    return config_.find(key) != config_.end();
}

void Manager::remove(const std::string& key) {
    std::lock_guard<std::mutex> lock(mutex_);
    config_.erase(key);
    watchers_.erase(key);
}

void Manager::clear() {
    std::lock_guard<std::mutex> lock(mutex_);
    config_.clear();
    watchers_.clear();
}

void Manager::watch(const std::string& key, ChangeCallback callback) {
    std::lock_guard<std::mutex> lock(mutex_);
    watchers_[key].push_back(callback);
}

void Manager::unwatch(const std::string& key) {
    std::lock_guard<std::mutex> lock(mutex_);
    watchers_.erase(key);
}

std::vector<std::string> Manager::keys() const {
    std::lock_guard<std::mutex> lock(mutex_);
    
    std::vector<std::string> result;
    for (const auto& [key, value] : config_) {
        result.push_back(key);
    }
    
    return result;
}

std::unordered_map<std::string, ConfigValue> Manager::all() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return config_;
}

// Global configuration manager
Manager& global_config() {
    static auto instance = Manager::create();
    return *instance;
}

} // namespace config
} // namespace best_server