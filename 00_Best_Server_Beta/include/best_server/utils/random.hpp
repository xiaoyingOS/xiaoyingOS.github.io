// Random - Random number generation utilities
//
// Provides utilities for generating random numbers and data:
// - Random bytes
// - Random strings
// - Random integers
// - Cryptographically secure random numbers

#ifndef BEST_SERVER_UTILS_RANDOM_HPP
#define BEST_SERVER_UTILS_RANDOM_HPP

#include <string>
#include <vector>
#include <cstdint>
#include <random>
#include <chrono>

namespace best_server {
namespace utils {

// Random number generator
class Random {
public:
    // Generate random bytes
    static void generate_bytes(std::vector<uint8_t>& buffer, size_t size);
    
    // Generate random bytes and return as string
    static std::string generate_random_bytes(size_t size);
    
    // Generate random integer in range [min, max]
    static int32_t random_int(int32_t min = 0, int32_t max = 2147483647);
    
    // Generate random 64-bit integer
    static uint64_t random_uint64();
    
    // Generate random 32-bit integer
    static uint32_t random_uint32();
    
    // Generate random 16-bit integer
    static uint16_t random_uint16();
    
    // Generate random string of given length
    static std::string random_string(size_t length);
    
    // Generate random alphanumeric string of given length
    static std::string random_alphanumeric(size_t length);
    
    // Generate random hex string of given length
    static std::string random_hex(size_t length);
    
    // Generate random base64 string
    static std::string random_base64(size_t length);
    
    // Generate UUID (version 4)
    static std::string generate_uuid();
    
    // Generate WebSocket accept key
    static std::string generate_websocket_key();
    
    // Shuffle vector
    template<typename T>
    static void shuffle(std::vector<T>& vec) {
        static thread_local std::random_device rd;
        static thread_local std::mt19937 gen(rd());
        std::shuffle(vec.begin(), vec.end(), gen);
    }
    
    // Sample n random elements from vector
    template<typename T>
    static std::vector<T> sample(const std::vector<T>& vec, size_t n) {
        std::vector<T> result(vec);
        shuffle(result);
        result.resize(std::min(n, vec.size()));
        return result;
    }
    
private:
    // Seeded random engine (thread-local)
    static thread_local std::mt19937_64 generator_;
    
    // Initialize generator
    static void ensure_initialized();
};

// Secure random (cryptographically secure)
class SecureRandom {
public:
    // Generate secure random bytes
    static bool generate_bytes(std::vector<uint8_t>& buffer, size_t size);
    
    // Generate secure random integer
    static bool random_uint64(uint64_t& value);
    
    // Generate secure random bytes as string
    static std::string generate_secure_bytes(size_t size);
    
    // Generate secure random string
    static std::string generate_secure_string(size_t length);
};

// UUID utilities
namespace uuid {
    // Generate v4 UUID (random)
    std::string v4();
    
    // Parse UUID string
    bool parse(const std::string& uuid_str, uint8_t uuid[16]);
    
    // Format UUID as string
    std::string format(const uint8_t uuid[16]);
    
    // Check if string is valid UUID
    bool is_valid(const std::string& uuid_str);
}

// Random utilities
namespace random {
    // Generate random boolean
    inline bool random_bool() {
        return Random::random_int(0, 1) == 1;
    }
    
    // Flip coin (50/50)
    inline bool flip_coin() {
        return random_bool();
    }
    
    // Random chance (0.0 to 1.0)
    inline bool chance(double probability) {
        return Random::random_int(0, 999999) < static_cast<int32_t>(probability * 1000000);
    }
    
    // Random choice from vector
    template<typename T>
    const T& choice(const std::vector<T>& vec) {
        if (vec.empty()) {
            static T empty_value;
            return empty_value;
        }
        return vec[Random::random_int(0, static_cast<int32_t>(vec.size() - 1))];
    }
    
    // Random choice from initializer list
    template<typename T>
    T choice(std::initializer_list<T> list) {
        if (list.size() == 0) {
            return T{};
        }
        auto it = list.begin();
        std::advance(it, Random::random_int(0, static_cast<int32_t>(list.size() - 1)));
        return *it;
    }
}

} // namespace utils
} // namespace best_server

#endif // BEST_SERVER_UTILS_RANDOM_HPP