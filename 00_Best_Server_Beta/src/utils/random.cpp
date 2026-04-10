// Random implementation

#include "best_server/utils/random.hpp"
#include <algorithm>
#include <cstring>
#include <random>
#include <sstream>
#include <iomanip>

#if defined(__linux__)
#include <sys/syscall.h>
#include <unistd.h>
#endif

namespace best_server {
namespace utils {

// Thread-local generator
thread_local std::mt19937_64 Random::generator_;

void Random::ensure_initialized() {
    // Initialize generator with high-entropy seed
    static thread_local bool initialized = false;
    if (!initialized) {
        std::random_device rd;
        generator_.seed(rd());
        initialized = true;
    }
}

void Random::generate_bytes(std::vector<uint8_t>& buffer, size_t size) {
    ensure_initialized();
    
    buffer.resize(size);
    for (size_t i = 0; i < size; ++i) {
        buffer[i] = static_cast<uint8_t>(generator_() & 0xFF);
    }
}

std::string Random::generate_random_bytes(size_t size) {
    std::vector<uint8_t> buffer;
    generate_bytes(buffer, size);
    return std::string(buffer.begin(), buffer.end());
}

int32_t Random::random_int(int32_t min, int32_t max) {
    ensure_initialized();
    std::uniform_int_distribution<int32_t> dist(min, max);
    return dist(generator_);
}

uint64_t Random::random_uint64() {
    ensure_initialized();
    return generator_();
}

uint32_t Random::random_uint32() {
    ensure_initialized();
    std::uniform_int_distribution<uint32_t> dist(0, 0xFFFFFFFF);
    return dist(generator_);
}

uint16_t Random::random_uint16() {
    ensure_initialized();
    std::uniform_int_distribution<uint16_t> dist(0, 0xFFFF);
    return dist(generator_);
}

std::string Random::random_string(size_t length) {
    static const char charset[] = 
        "abcdefghijklmnopqrstuvwxyz"
        "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
        "0123456789"
        "!@#$%^&*()_+-=[]{}|;:,.<>?";
    
    ensure_initialized();
    std::uniform_int_distribution<size_t> dist(0, sizeof(charset) - 2);
    
    std::string result;
    result.reserve(length);
    
    for (size_t i = 0; i < length; ++i) {
        result += charset[dist(generator_)];
    }
    
    return result;
}

std::string Random::random_alphanumeric(size_t length) {
    static const char charset[] = 
        "abcdefghijklmnopqrstuvwxyz"
        "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
        "0123456789";
    
    ensure_initialized();
    std::uniform_int_distribution<size_t> dist(0, sizeof(charset) - 2);
    
    std::string result;
    result.reserve(length);
    
    for (size_t i = 0; i < length; ++i) {
        result += charset[dist(generator_)];
    }
    
    return result;
}

std::string Random::random_hex(size_t length) {
    static const char charset[] = "0123456789abcdef";
    
    ensure_initialized();
    std::uniform_int_distribution<size_t> dist(0, 15);
    
    std::string result;
    result.reserve(length);
    
    for (size_t i = 0; i < length; ++i) {
        result += charset[dist(generator_)];
    }
    
    return result;
}

std::string Random::random_base64(size_t length) {
    static const char charset[] = 
        "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
        "abcdefghijklmnopqrstuvwxyz"
        "0123456789+/";
    
    ensure_initialized();
    std::uniform_int_distribution<size_t> dist(0, 63);
    
    std::string result;
    result.reserve(length);
    
    for (size_t i = 0; i < length; ++i) {
        result += charset[dist(generator_)];
    }
    
    return result;
}

std::string Random::generate_uuid() {
    std::vector<uint8_t> uuid_bytes(16);
    generate_bytes(uuid_bytes, 16);
    
    // Set version bits (version 4)
    uuid_bytes[6] = (uuid_bytes[6] & 0x0F) | 0x40;  // Version 4
    uuid_bytes[8] = (uuid_bytes[8] & 0x3F) | 0x80;  // Variant 1
    
    std::ostringstream oss;
    oss << std::hex << std::setfill('0');
    
    for (size_t i = 0; i < 16; ++i) {
        if (i == 4 || i == 6 || i == 8 || i == 10) {
            oss << '-';
        }
        oss << std::setw(2) << static_cast<int>(uuid_bytes[i]);
    }
    
    return oss.str();
}

std::string Random::generate_websocket_key() {
    // Generate 16 random bytes and base64 encode
    std::vector<uint8_t> key_bytes(16);
    generate_bytes(key_bytes, 16);
    
    // Simple base64 encoding
    static const char b64chars[] = 
        "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
        "abcdefghijklmnopqrstuvwxyz"
        "0123456789+/";
    
    std::string result;
    size_t i = 0;
    
    while (i < key_bytes.size()) {
        uint32_t triple = (static_cast<uint32_t>(key_bytes[i]) << 16);
        
        if (i + 1 < key_bytes.size()) {
            triple |= (static_cast<uint32_t>(key_bytes[i + 1]) << 8);
        }
        
        if (i + 2 < key_bytes.size()) {
            triple |= key_bytes[i + 2];
        }
        
        result += b64chars[(triple >> 18) & 0x3F];
        result += b64chars[(triple >> 12) & 0x3F];
        result += b64chars[(triple >> 6) & 0x3F];
        result += b64chars[triple & 0x3F];
        
        i += 3;
    }
    
    // Pad with '=' if necessary
    while (result.size() % 4 != 0) {
        result += '=';
    }
    
    return result;
}

// SecureRandom implementation
bool SecureRandom::generate_bytes(std::vector<uint8_t>& buffer, size_t size) {
#if defined(__linux__) && defined(__NR_getrandom)
    buffer.resize(size);
    ssize_t bytes_read = syscall(__NR_getrandom, buffer.data(), size, 0);
    return bytes_read == static_cast<ssize_t>(size);
#else
    // Fallback to regular random for non-Linux systems
    Random::generate_bytes(buffer, size);
    return true;
#endif
}

bool SecureRandom::random_uint64(uint64_t& value) {
    std::vector<uint8_t> buffer(sizeof(uint64_t));
    if (!generate_bytes(buffer, sizeof(uint64_t))) {
        return false;
    }
    std::memcpy(&value, buffer.data(), sizeof(uint64_t));
    return true;
}

std::string SecureRandom::generate_secure_bytes(size_t size) {
    std::vector<uint8_t> buffer;
    if (!generate_bytes(buffer, size)) {
        return "";
    }
    return std::string(buffer.begin(), buffer.end());
}

std::string SecureRandom::generate_secure_string(size_t length) {
    std::vector<uint8_t> buffer(length);
    if (!generate_bytes(buffer, length)) {
        return "";
    }
    
    static const char charset[] = 
        "abcdefghijklmnopqrstuvwxyz"
        "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
        "0123456789"
        "!@#$%^&*()_+-=[]{}|;:,.<>?";
    
    std::string result;
    result.reserve(length);
    
    for (size_t i = 0; i < length; ++i) {
        result += charset[buffer[i] % (sizeof(charset) - 1)];
    }
    
    return result;
}

// UUID namespace implementation
namespace uuid {

std::string v4() {
    return Random::generate_uuid();
}

bool parse(const std::string& uuid_str, uint8_t uuid[16]) {
    if (uuid_str.length() != 36) {
        return false;
    }
    
    // Check format: 8-4-4-4-12
    if (uuid_str[8] != '-' || uuid_str[13] != '-' || 
        uuid_str[18] != '-' || uuid_str[23] != '-') {
        return false;
    }
    
    std::string clean_str = uuid_str;
    clean_str.erase(std::remove(clean_str.begin(), clean_str.end(), '-'), clean_str.end());
    
    if (clean_str.length() != 32) {
        return false;
    }
    
    for (size_t i = 0; i < 16; ++i) {
        std::string byte_str = clean_str.substr(i * 2, 2);
        try {
            uuid[i] = static_cast<uint8_t>(std::stoul(byte_str, nullptr, 16));
        } catch (...) {
            return false;
        }
    }
    
    return true;
}

std::string format(const uint8_t uuid[16]) {
    std::ostringstream oss;
    oss << std::hex << std::setfill('0');
    
    for (size_t i = 0; i < 16; ++i) {
        if (i == 4 || i == 6 || i == 8 || i == 10) {
            oss << '-';
        }
        oss << std::setw(2) << static_cast<int>(uuid[i]);
    }
    
    return oss.str();
}

bool is_valid(const std::string& uuid_str) {
    uint8_t uuid[16];
    return parse(uuid_str, uuid);
}

} // namespace uuid

} // namespace utils
} // namespace best_server