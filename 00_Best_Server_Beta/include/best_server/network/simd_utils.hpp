// SIMD优化工具函数 - 用于HTTP解析加速

#ifndef BEST_SERVER_NETWORK_SIMD_UTILS_HPP
#define BEST_SERVER_NETWORK_SIMD_UTILS_HPP

#include <cstdint>
#include <cstring>
#include <algorithm>

#if defined(__x86_64__) || defined(__i386__)
#include <immintrin.h>  // For SSE/AVX intrinsics
#elif defined(__aarch64__)
#include <arm_neon.h>   // For ARM NEON intrinsics
#endif

namespace best_server {
namespace network {

// SIMD优化的字符串搜索函数
class SIMDUtils {
public:
    // 使用SIMD查找字符
    static const char* find_char_simd(const char* data, size_t size, char c) {
        // Simple implementation without SIMD to avoid bugs
        return static_cast<const char*>(memchr(data, c, size));
    }
    
    // 使用SIMD查找CRLF
    static const char* find_crlf_simd(const char* data, size_t size) {
        // Simple implementation without SIMD to avoid bugs
        for (size_t i = 0; i < size - 1; ++i) {
            if (data[i] == '\r' && data[i + 1] == '\n') {
                return data + i;
            }
        }
        return data + size;
    }
    
    // 使用SIMD查找多个字符中的任意一个
    static const char* find_any_char_simd(const char* data, size_t size, const char* chars, size_t num_chars) {
#if defined(__x86_64__) || defined(__i386__)
        // 使用SSE4.1优化
        const size_t simd_size = sizeof(__m128i);
        
        size_t i = 0;
        const size_t limit = size - simd_size;
        
        // 创建掩码
        __m128i cmp_mask = _mm_set1_epi8(0);
        for (size_t j = 0; j < num_chars; ++j) {
            __m128i char_pattern = _mm_set1_epi8(chars[j]);
            __m128i temp = _mm_cmpeq_epi8(_mm_loadu_si128(reinterpret_cast<const __m128i*>(data + i)), char_pattern);
            cmp_mask = _mm_or_si128(cmp_mask, temp);
        }
        
        for (; i <= limit; i += simd_size) {
            __m128i chunk = _mm_loadu_si128(reinterpret_cast<const __m128i*>(data + i));
            __m128i cmp = _mm_cmpeq_epi8(chunk, cmp_mask);
            unsigned mask = _mm_movemask_epi8(cmp);
            
            if (mask) {
                return data + i + __builtin_ctz(mask);
            }
        }
        
        // 处理剩余部分
        for (; i < size; ++i) {
            for (size_t j = 0; j < num_chars; ++j) {
                if (data[i] == chars[j]) {
                    return data + i;
                }
            }
        }
        
        return data + size;
        
#else
        // 回退到普通搜索
        for (size_t i = 0; i < size; ++i) {
            for (size_t j = 0; j < num_chars; ++j) {
                if (data[i] == chars[j]) {
                    return data + i;
                }
            }
        }
        return data + size;
#endif
    }
};

} // namespace network
} // namespace best_server

#endif // BEST_SERVER_NETWORK_SIMD_UTILS_HPP