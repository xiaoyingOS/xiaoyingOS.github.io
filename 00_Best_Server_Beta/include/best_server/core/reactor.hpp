// Reactor - I/O event loop with zero-copy support
// 
// Implements an optimized event loop supporting:
// - epoll (Linux)
// - kqueue (macOS/BSD)
// - IOCP (Windows)
// - Zero-copy buffer management
// - Edge-triggered I/O
// - NUMA-aware memory allocation

#ifndef BEST_SERVER_CORE_REACTOR_HPP
#define BEST_SERVER_CORE_REACTOR_HPP

#include <memory>
#include <functional>
#include <unordered_map>
#include <atomic>
#include <vector>
#include <cstdint>

#if BEST_SERVER_PLATFORM_LINUX
    #include <sys/epoll.h>
#elif BEST_SERVER_PLATFORM_MACOS
    #include <sys/types.h>
    #include <sys/event.h>
#elif BEST_SERVER_PLATFORM_WINDOWS
    #include <windows.h>
#endif

namespace best_server {
namespace core {

// Forward declarations
namespace memory {
    class ZeroCopyBuffer;
}

// I/O event types
enum class IOEventType : uint8_t {
    Read = 0x01,
    Write = 0x02,
    Accept = 0x04,
    Connect = 0x08,
    Error = 0x10
};

// I/O event handler
using IOEventHandler = std::function<void(IOEventType, int fd)>;

// File descriptor registration
struct FDRegistration {
    int fd;
    IOEventHandler handler;
    IOEventType events;
    void* user_data;
};

// Reactor statistics
struct ReactorStats {
    uint64_t events_processed{0};
    uint64_t events_dispatched{0};
    uint64_t poll_count{0};
    uint64_t idle_count{0};
    uint64_t wake_count{0};
    uint64_t bytes_read{0};
    uint64_t bytes_written{0};
    uint32_t registered_fds{0};
};

// Reactor - optimized event loop
class Reactor {
public:
    Reactor();
    ~Reactor();
    
    // Start the event loop
    void run();
    
    // Stop the event loop
    void stop();
    
    // Run one iteration of the event loop
    bool run_once(int timeout_ms = 100);
    
    // Register a file descriptor
    bool register_fd(int fd, IOEventType events, IOEventHandler handler);
    
    // Modify existing registration
    bool modify_fd(int fd, IOEventType events);
    
    // Unregister a file descriptor
    bool unregister_fd(int fd);
    
    // Wake up the reactor (for external notifications)
    void wake_up();
    
    // Get statistics
    const ReactorStats& stats() const { return stats_; }
    
    // Check if running
    bool is_running() const { return running_.load(); }
    
    // Get current time (cached for performance)
    uint64_t current_time_ms() const { return current_time_ms_; }
    
private:
    void event_loop();
    bool poll_events(int timeout_ms);
    void handle_events();
    void update_time();
    
#if BEST_SERVER_PLATFORM_LINUX
    struct epoll_event* epoll_events_;
    int epoll_fd_;
#elif BEST_SERVER_PLATFORM_MACOS
    struct kevent* kqueue_events_;
    int kqueue_fd_;
#elif BEST_SERVER_PLATFORM_WINDOWS
    HANDLE iocp_port_;
    OVERLAPPED_ENTRY* iocp_events_;
#endif
    
    std::atomic<bool> running_{false};
    int wake_fd_[2];  // Pipe for wake-up
    
    // Optimized hash map for file descriptors
    struct FDHash {
        size_t operator()(int fd) const noexcept {
            return static_cast<size_t>(fd);
        }
    };
    std::unordered_map<int, FDRegistration, FDHash> fd_map_;
    
    ReactorStats stats_;
    
    uint64_t current_time_ms_{0};
    static constexpr int MAX_EVENTS = 1024;
};

// Zero-copy I/O buffer
class IOBuffer {
public:
    static constexpr size_t DEFAULT_SIZE = 64 * 1024; // 64KB
    
    IOBuffer(size_t capacity = DEFAULT_SIZE);
    ~IOBuffer();
    
    // Buffer access
    char* data() { return data_.get(); }
    const char* data() const { return data_.get(); }
    size_t capacity() const { return capacity_; }
    size_t size() const { return size_; }
    size_t remaining() const { return capacity_ - size_; }
    size_t available() const { return size_ - read_pos_; }
    
    // Read/write operations
    bool write(const char* src, size_t len);
    bool read(char* dst, size_t len);
    void consume(size_t len);
    void clear();
    
    // Reserve space
    bool reserve(size_t len);
    
private:
    std::unique_ptr<char[]> data_;
    size_t capacity_;
    size_t size_;
    size_t read_pos_;
};

} // namespace core
} // namespace best_server

#endif // BEST_SERVER_CORE_REACTOR_HPP