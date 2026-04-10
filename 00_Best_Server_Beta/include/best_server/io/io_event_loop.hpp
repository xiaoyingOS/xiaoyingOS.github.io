// IOEventLoop - Cross-platform I/O event loop abstraction
// 
// Provides a unified interface for:
// - epoll (Linux)
// - kqueue (macOS/BSD)
// - IOCP (Windows)
// - Asynchronous file I/O

#ifndef BEST_SERVER_IO_IO_EVENT_LOOP_HPP
#define BEST_SERVER_IO_IO_EVENT_LOOP_HPP

#include <functional>
#include <memory>
#include <vector>
#include <atomic>
#include <thread>
#include <unordered_map>
#include <mutex>

// Platform-specific headers
#if defined(__linux__)
#include <sys/epoll.h>
#elif defined(__APPLE__) && defined(__MACH__)
#include <sys/types.h>
#include <sys/event.h>
#elif defined(_WIN32) || defined(_WIN64)
#include <windows.h>
#endif
#include <cstdint>

namespace best_server {
namespace io {

// Event type flags
enum class EventType : uint32_t {
    None = 0x00,
    Read = 0x01,
    Write = 0x02,
    Accept = 0x04,
    Connect = 0x04,
    Error = 0x08,
    Hangup = 0x10,
    OneShot = 0x20,
    EdgeTriggered = 0x40
};

inline EventType operator|(EventType a, EventType b) {
    return static_cast<EventType>(static_cast<uint32_t>(a) | static_cast<uint32_t>(b));
}

inline EventType operator&(EventType a, EventType b) {
    return static_cast<EventType>(static_cast<uint32_t>(a) & static_cast<uint32_t>(b));
}

inline bool operator!=(EventType a, EventType b) {
    return static_cast<uint32_t>(a) != static_cast<uint32_t>(b);
}

// Event callback
using EventCallback = std::function<void(EventType)>;

// File descriptor registration
struct FDRegistration {
    int fd;
    EventCallback callback;
    EventType events;
    void* user_data;
};

// Event loop statistics
struct EventLoopStats {
    uint64_t events_processed{0};
    uint64_t events_dispatched{0};
    uint64_t poll_count{0};
    uint64_t wake_count{0};
    uint32_t registered_fds{0};
};

// Cross-platform event loop
class IOEventLoop {
public:
    IOEventLoop();
    ~IOEventLoop();
    
    // Run the event loop
    void run();
    
    // Stop the event loop
    void stop();
    
    // Register a file descriptor
    bool register_fd(int fd, EventType events, EventCallback callback);
    
    // Modify registration
    bool modify_fd(int fd, EventType events);
    
    // Unregister a file descriptor
    bool unregister_fd(int fd);
    
    // Wake up the event loop
    void wake_up();
    
    // Get statistics
    const EventLoopStats& stats() const { return stats_; }
    
    // Check if running
    bool is_running() const { return running_.load(); }
    
    // Run once (non-blocking)
    bool run_once(int timeout_ms = 0);
    
    // Schedule a task to run on the event loop
    void schedule(std::function<void()> task);
    
    // Get current thread's event loop
    static IOEventLoop* current();
    
private:
    void event_loop_thread();
    bool poll_events(int timeout_ms);
    void dispatch_events();
    void run_pending_tasks();
    
#if defined(__linux__)
    struct epoll_event* epoll_events_;
    int epoll_fd_;
#elif defined(__APPLE__) && defined(__MACH__)
    struct kevent* kqueue_events_;
    int kqueue_fd_;
#elif defined(_WIN32) || defined(_WIN64)
    HANDLE iocp_port_;
    OVERLAPPED_ENTRY* iocp_events_;
#endif
    
    std::atomic<bool> running_{false};
    int wake_pipe_[2];
    
    std::unordered_map<int, FDRegistration> fd_map_;
    EventLoopStats stats_;
    
    // Task queue for scheduled tasks
    std::vector<std::function<void()>> task_queue_;
    std::mutex task_queue_mutex_;
    
    // Global map of event loops per thread
    static std::unordered_map<std::thread::id, IOEventLoop*> g_event_loops_;
    static std::mutex g_event_loops_mutex_;
    
    static constexpr int MAX_EVENTS = 1024;
};

// Asynchronous operation result
template<typename T>
class AsyncResult {
public:
    AsyncResult() : completed_(false) {}
    
    bool is_completed() const { return completed_.load(); }
    
    const T& get() const {
        if (!is_completed()) {
            throw std::runtime_error("Operation not completed");
        }
        return result_;
    }
    
    void set_result(const T& result) {
        result_ = result;
        completed_.store(true);
    }
    
    void set_result(T&& result) {
        result_ = std::move(result);
        completed_.store(true);
    }
    
private:
    std::atomic<bool> completed_;
    T result_;
};

} // namespace io
} // namespace best_server

#endif // BEST_SERVER_IO_IO_EVENT_LOOP_HPP