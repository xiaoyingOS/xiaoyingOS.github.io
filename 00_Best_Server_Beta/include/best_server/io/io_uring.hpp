// io_uring Support - Linux async I/O support
//
// Provides io_uring integration for high-performance async I/O
// Note: io_uring requires Linux kernel 5.1+
// This implementation provides a fallback to epoll on systems without io_uring

#ifndef BEST_SERVER_IO_IO_URING_HPP
#define BEST_SERVER_IO_IO_URING_HPP

#include <memory>
#include <vector>
#include <functional>
#include <atomic>
#include <cstdint>

#if defined(__linux__)
    #include <sys/types.h>
    #include <sys/socket.h>
    #include <unistd.h>
    #include <errno.h>
#endif

namespace best_server {
namespace io {

// Forward declarations
class IoUringImpl;

// io_uring operation types
enum class IoOpType {
    Read,
    Write,
    Accept,
    Connect,
    Send,
    Recv,
    Close,
    Max
};

// io_uring operation result
struct IoResult {
    int result;        // Operation result (bytes transferred or error code)
    int error;         // Error code if result < 0
    void* user_data;   // User-provided data
};

// io_uring completion callback
using IoCallback = std::function<void(const IoResult&)>;

// io_uring configuration
struct IoUringConfig {
    size_t queue_entries;    // Number of queue entries
    size_t sq_entries;       // Submission queue entries
    size_t cq_entries;       // Completion queue entries
    bool enable_sqpoll;      // Enable submission queue polling
    unsigned sq_thread_idle; // SQ polling thread idle time in ms
    unsigned sq_thread_cpu;  // CPU to pin SQ polling thread
    bool enable_cqwait;      // Enable completion queue waiting
    
    IoUringConfig()
        : queue_entries(256)
        , sq_entries(256)
        , cq_entries(256)
        , enable_sqpoll(false)
        , sq_thread_idle(1000)
        , sq_thread_cpu(0)
        , enable_cqwait(true)
    {}
};

// io_uring wrapper
class IoUring {
public:
    IoUring();
    explicit IoUring(const IoUringConfig& config);
    ~IoUring();
    
    // Check if io_uring is available
    bool is_available() const;
    
    // Initialize io_uring
    bool initialize(const IoUringConfig& config);
    
    // Shutdown io_uring
    void shutdown();
    
    // Submit read operation
    bool submit_read(int fd, void* buffer, size_t count, off_t offset,
                    IoCallback callback, void* user_data = nullptr);
    
    // Submit write operation
    bool submit_write(int fd, const void* buffer, size_t count, off_t offset,
                     IoCallback callback, void* user_data = nullptr);
    
    // Submit accept operation
    bool submit_accept(int fd, sockaddr* addr, socklen_t* addrlen,
                      IoCallback callback, void* user_data = nullptr);
    
    // Submit connect operation
    bool submit_connect(int fd, const sockaddr* addr, socklen_t addrlen,
                       IoCallback callback, void* user_data = nullptr);
    
    // Submit send operation
    bool submit_send(int fd, const void* buffer, size_t count, int flags,
                    IoCallback callback, void* user_data = nullptr);
    
    // Submit recv operation
    bool submit_recv(int fd, void* buffer, size_t count, int flags,
                    IoCallback callback, void* user_data = nullptr);
    
    // Submit close operation
    bool submit_close(int fd, IoCallback callback, void* user_data = nullptr);
    
    // Process completions
    size_t process_completions(size_t max_events = 64);
    
    // Wait for events
    int wait_for_events(size_t min_events = 1, int timeout_ms = -1);
    
    // Get pending submission count
    size_t pending_submissions() const;
    
    // Get pending completion count
    size_t pending_completions() const;
    
    // Submit pending operations
    int submit();
    
    // Get statistics
    struct Stats {
        uint64_t submissions;
        uint64_t completions;
        uint64_t errors;
        uint64_t submissions_failed;
    };
    
    Stats get_stats() const;
    
private:
    std::unique_ptr<IoUringImpl> impl_;
    bool initialized_;
};

// Helper class for io_uring file I/O
class IoUringFile {
public:
    IoUringFile(IoUring* io_uring, int fd);
    ~IoUringFile();
    
    // Async read
    bool read(void* buffer, size_t count, off_t offset,
             IoCallback callback, void* user_data = nullptr);
    
    // Async write
    bool write(const void* buffer, size_t count, off_t offset,
              IoCallback callback, void* user_data = nullptr);
    
    // Async close
    bool close(IoCallback callback, void* user_data = nullptr);
    
    int fd() const { return fd_; }
    
private:
    IoUring* io_uring_;
    int fd_;
};

// Helper class for io_uring socket I/O
class IoUringSocket {
public:
    IoUringSocket(IoUring* io_uring, int fd);
    ~IoUringSocket();
    
    // Async accept
    bool accept(sockaddr* addr, socklen_t* addrlen,
              IoCallback callback, void* user_data = nullptr);
    
    // Async connect
    bool connect(const sockaddr* addr, socklen_t addrlen,
                IoCallback callback, void* user_data = nullptr);
    
    // Async send
    bool send(const void* buffer, size_t count, int flags,
             IoCallback callback, void* user_data = nullptr);
    
    // Async recv
    bool recv(void* buffer, size_t count, int flags,
             IoCallback callback, void* user_data = nullptr);
    
    // Async close
    bool close(IoCallback callback, void* user_data = nullptr);
    
    int fd() const { return fd_; }
    
private:
    IoUring* io_uring_;
    int fd_;
};

// io_uring factory
class IoUringFactory {
public:
    // Create io_uring instance
    static std::unique_ptr<IoUring> create(const IoUringConfig& config = IoUringConfig());
    
    // Check if io_uring is supported
    static bool is_supported();
    
    // Get supported io_uring version
    static int get_version();
    
    // Get supported features
    static uint32_t get_features();
};

} // namespace io
} // namespace best_server

#endif // BEST_SERVER_IO_IO_URING_HPP