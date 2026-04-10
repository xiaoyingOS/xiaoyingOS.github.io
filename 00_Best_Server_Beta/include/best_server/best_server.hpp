// Best_Server - High Performance Multi-Platform Async Server Framework
// Copyright (c) 2026
// 
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
// 
//     http://www.apache.org/licenses/LICENSE-2.0
// 
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#ifndef BEST_SERVER_HPP
#define BEST_SERVER_HPP

#include <cstdint>
#include <memory>

// Platform detection
#if defined(_WIN32) || defined(_WIN64)
    #define BEST_SERVER_PLATFORM_WINDOWS 1
#elif defined(__APPLE__) && defined(__MACH__)
    #define BEST_SERVER_PLATFORM_MACOS 1
#elif defined(__linux__)
    #define BEST_SERVER_PLATFORM_LINUX 1
#else
    #define BEST_SERVER_PLATFORM_UNKNOWN 1
#endif

// Compiler feature detection
#if defined(__cpp_lib_coroutine) && __cpp_lib_coroutine >= 201902L
    #define BEST_SERVER_HAS_COROUTINES 1
#endif

namespace best_server {

// Version information
constexpr uint32_t BEST_SERVER_VERSION_MAJOR = 1;
constexpr uint32_t BEST_SERVER_VERSION_MINOR = 0;
constexpr uint32_t BEST_SERVER_VERSION_PATCH = 0;

} // namespace best_server

// Include all component headers outside the namespace to avoid namespace conflicts
// Core components
#include "core/scheduler.hpp"
#include "core/reactor.hpp"
#include "core/task_queue.hpp"
#include "core/thread_pool.hpp"

// Memory management
#include "memory/allocator.hpp"
#include "memory/object_pool.hpp"
#include "memory/zero_copy_buffer.hpp"

// I/O operations
#include "io/io_event_loop.hpp"
#include "io/tcp_socket.hpp"
#include "io/udp_socket.hpp"
#include "io/file_reader.hpp"
#include "io/file_writer.hpp"

// Future/Promise model
#include "future/future.hpp"

// Timer system
#include "timer/timer_wheel.hpp"
#include "timer/timer_manager.hpp"

// Network
#include "network/http_server.hpp"
#include "network/http_request.hpp"
#include "network/http_response.hpp"
#include "network/http_parser.hpp"

// RPC
#include "rpc/rpc_server.hpp"
#include "rpc/rpc_client.hpp"
#include "rpc/rpc_protocol.hpp"

// Configuration
#include "config/config_manager.hpp"

// Monitoring (temporarily disabled - atomic vector issues)
// #include "monitoring/monitoring.hpp"

// Load balancer
#include "load_balancer/load_balancer.hpp"

// Service discovery
#include "service_discovery/service_discovery.hpp"

// Logger (temporarily disabled - namespace issues)
// #include "logger/logger.hpp"

// Health checker (temporarily disabled)
// #include "health/health_checker.hpp"

// Utility functions
namespace best_server {
namespace utils {

// Get number of CPU cores
inline int cpu_count() {
#if BEST_SERVER_PLATFORM_LINUX
#ifdef __ANDROID__
    return 1; // Android doesn't have sysconf or returns incorrect values
#else
    return sysconf(_SC_NPROCESSORS_ONLN);
#endif
#elif BEST_SERVER_PLATFORM_MACOS
    int count;
    size_t size = sizeof(count);
    return sysctlbyname("hw.ncpu", &count, &size, nullptr, 0) ? 1 : count;
#elif BEST_SERVER_PLATFORM_WINDOWS
    SYSTEM_INFO sysinfo;
    GetSystemInfo(&sysinfo);
    return sysinfo.dwNumberOfProcessors;
#else
    return 1;
#endif
}

// Get current thread ID
inline uint64_t thread_id() {
#if BEST_SERVER_PLATFORM_LINUX || BEST_SERVER_PLATFORM_MACOS
    return (uint64_t)pthread_self();
#elif BEST_SERVER_PLATFORM_WINDOWS
    return (uint64_t)GetCurrentThreadId();
#else
    return 0;
#endif
}

// Get current CPU core
inline int current_cpu() {
#if BEST_SERVER_PLATFORM_LINUX
    return sched_getcpu();
#elif BEST_SERVER_PLATFORM_MACOS
    thread_port_t thread = pthread_mach_thread_np(pthread_self());
    return thread_info(thread, THREAD_BASIC_INFO, nullptr, 0);
#else
    return 0;
#endif
}

} // namespace utils
} // namespace best_server

#endif // BEST_SERVER_HPP