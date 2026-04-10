// ORM implementation
#include "best_server/database/orm/orm.hpp"
#include <sstream>
#include <regex>
#include <cstring>

namespace best_server {
namespace database {
namespace orm {

// Validation utilities implementation
namespace validation {

std::vector<ValidationError> validate_required(const std::string& value, const std::string& field_name) {
    std::vector<ValidationError> errors;
    
    if (value.empty()) {
        errors.push_back({field_name, field_name + " is required"});
    }
    
    return errors;
}

std::vector<ValidationError> validate_length(const std::string& value, size_t min, size_t max, const std::string& field_name) {
    std::vector<ValidationError> errors;
    
    size_t length = value.length();
    
    if (length < min) {
        errors.push_back({field_name, field_name + " must be at least " + std::to_string(min) + " characters"});
    }
    
    if (length > max) {
        errors.push_back({field_name, field_name + " must be at most " + std::to_string(max) + " characters"});
    }
    
    return errors;
}

std::vector<ValidationError> validate_email(const std::string& value, const std::string& field_name) {
    std::vector<ValidationError> errors;
    
    static const std::regex email_regex(R"(^[a-zA-Z0-9._%+-]+@[a-zA-Z0-9.-]+\.[a-zA-Z]{2,}$)");
    
    if (!std::regex_match(value, email_regex)) {
        errors.push_back({field_name, field_name + " must be a valid email address"});
    }
    
    return errors;
}

std::vector<ValidationError> validate_pattern(const std::string& value, const std::string& pattern, const std::string& field_name) {
    std::vector<ValidationError> errors;
    
    try {
        std::regex regex(pattern);
        if (!std::regex_match(value, regex)) {
            errors.push_back({field_name, field_name + " does not match the required pattern"});
        }
    } catch (const std::regex_error& e) {
        errors.push_back({field_name, "Invalid regex pattern: " + std::string(e.what())});
    }
    
    return errors;
}

std::vector<ValidationError> validate_range(int64_t value, int64_t min, int64_t max, const std::string& field_name) {
    std::vector<ValidationError> errors;
    
    if (value < min) {
        errors.push_back({field_name, field_name + " must be at least " + std::to_string(min)});
    }
    
    if (value > max) {
        errors.push_back({field_name, field_name + " must be at most " + std::to_string(max)});
    }
    
    return errors;
}

} // namespace validation

} // namespace orm
} // namespace database
} // namespace best_server