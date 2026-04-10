// OptimizedSocket - High-performance socket operations
// 
// Provides optimized socket I/O with:
// - Batch operations
// - Vectorized I/O (readv/writev)
// - Recvmmsg/sendmmsg support
// - Zero-copy sendfile
// - TCP Fast Open
// - Socket option tuning

#ifndef BEST_SERVER_IO_OPTIMIZED_SOCKET_HPP
#define BEST_SERVER_IO_OPTIMIZED_SOCKET_HPP

#include <vector>
#include <array>
#include <cstdint>
#include <cstring>

#if BEST_SERVER_PLATFORM_LINUX
#include <sys/socket.h>
#include <sys/uio.h>
#include <sys/sendfile.h>
#include <netinet/tcp.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#endif

namespace best_server {
namespace io {

// Optimized socket operations
class OptimizedSocket {
public:
    // Batch read using readv
    static ssize_t readv_batch(int fd, const std::vector<iovec>& iov) {
#if BEST_SERVER_PLATFORM_LINUX
        return ::readv(fd, iov.data(), iov.size());
#else
        // Fallback
        return 0;
#endif
    }
    
    // Batch write using writev
    static ssize_t writev_batch(int fd, const std::vector<iovec>& iov) {
#if BEST_SERVER_PLATFORM_LINUX
        return ::writev(fd, iov.data(), iov.size());
#else
        // Fallback
        return 0;
#endif
    }
    
    // Recvmmsg for batch message receive
    static ssize_t recv_mmsg_batch(int fd, std::vector<mmsghdr>& msgs, int flags = 0) {
#if BEST_SERVER_PLATFORM_LINUX
        return ::recvmmsg(fd, msgs.data(), msgs.size(), flags, nullptr);
#else
        // Fallback
        return 0;
#endif
    }
    
    // Sendmmsg for batch message send
    static ssize_t send_mmsg_batch(int fd, std::vector<mmsghdr>& msgs, int flags = 0) {
#if BEST_SERVER_PLATFORM_LINUX
        return ::sendmmsg(fd, msgs.data(), msgs.size(), flags);
#else
        // Fallback
        return 0;
#endif
    }
    
    // Zero-copy file send using sendfile
    static ssize_t sendfile_zero_copy(int out_fd, int in_fd, off_t* offset, size_t count) {
#if BEST_SERVER_PLATFORM_LINUX
        return ::sendfile(out_fd, in_fd, offset, count);
#else
        // Fallback
        return 0;
#endif
    }
    
    // Set TCP Fast Open
    static bool set_tcp_fast_open(int fd, int qlen = 5) {
#if BEST_SERVER_PLATFORM_LINUX
        int optval = qlen;
        return setsockopt(fd, IPPROTO_TCP, TCP_FASTOPEN, &optval, sizeof(optval)) == 0;
#else
        return false;
#endif
    }
    
    // Set TCP No Delay (disable Nagle)
    static bool set_tcp_no_delay(int fd, bool enable = true) {
        int optval = enable ? 1 : 0;
        return setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &optval, sizeof(optval)) == 0;
    }
    
    // Set TCP Keepalive
    static bool set_tcp_keepalive(int fd, bool enable = true, int idle = 60, int interval = 5, int count = 3) {
#if BEST_SERVER_PLATFORM_LINUX
        int optval = enable ? 1 : 0;
        if (setsockopt(fd, SOL_SOCKET, SO_KEEPALIVE, &optval, sizeof(optval)) != 0) {
            return false;
        }
        
        optval = idle;
        if (setsockopt(fd, IPPROTO_TCP, TCP_KEEPIDLE, &optval, sizeof(optval)) != 0) {
            return false;
        }
        
        optval = interval;
        if (setsockopt(fd, IPPROTO_TCP, TCP_KEEPINTVL, &optval, sizeof(optval)) != 0) {
            return false;
        }
        
        optval = count;
        if (setsockopt(fd, IPPROTO_TCP, TCP_KEEPCNT, &optval, sizeof(optval)) != 0) {
            return false;
        }
        
        return true;
#else
        return false;
#endif
    }
    
    // Set TCP Quick ACK
    static bool set_tcp_quick_ack(int fd, bool enable = true) {
#if BEST_SERVER_PLATFORM_LINUX
        int optval = enable ? 1 : 0;
        return setsockopt(fd, IPPROTO_TCP, TCP_QUICKACK, &optval, sizeof(optval)) == 0;
#else
        return false;
#endif
    }
    
    // Set TCP Window Scaling
    static bool set_tcp_window_scaling(int fd, bool enable = true) {
#if BEST_SERVER_PLATFORM_LINUX
        int optval = enable ? 1 : 0;
        return setsockopt(fd, IPPROTO_TCP, TCP_WINDOW_CLAMP, &optval, sizeof(optval)) == 0;
#else
        return false;
#endif
    }
    
    // Set SO_REUSEADDR
    static bool set_reuse_addr(int fd, bool enable = true) {
        int optval = enable ? 1 : 0;
        return setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval)) == 0;
    }
    
    // Set SO_REUSEPORT
    static bool set_reuse_port(int fd, bool enable = true) {
#if BEST_SERVER_PLATFORM_LINUX
        int optval = enable ? 1 : 0;
        return setsockopt(fd, SOL_SOCKET, SO_REUSEPORT, &optval, sizeof(optval)) == 0;
#else
        return false;
#endif
    }
    
    // Set SO_SNDBUF
    static bool set_send_buffer(int fd, size_t size) {
        int optval = static_cast<int>(size);
        return setsockopt(fd, SOL_SOCKET, SO_SNDBUF, &optval, sizeof(optval)) == 0;
    }
    
    // Set SO_RCVBUF
    static bool set_receive_buffer(int fd, size_t size) {
        int optval = static_cast<int>(size);
        return setsockopt(fd, SOL_SOCKET, SO_RCVBUF, &optval, sizeof(optval)) == 0;
    }
    
    // Set non-blocking mode
    static bool set_non_blocking(int fd) {
#if BEST_SERVER_PLATFORM_LINUX
        int flags = fcntl(fd, F_GETFL, 0);
        return fcntl(fd, F_SETFL, flags | O_NONBLOCK) != -1;
#else
        return false;
#endif
    }
    
    // Set close-on-exec
    static bool set_close_on_exec(int fd) {
#if BEST_SERVER_PLATFORM_LINUX
        int flags = fcntl(fd, F_GETFD, 0);
        return fcntl(fd, F_SETFD, flags | FD_CLOEXEC) != -1;
#else
        return false;
#endif
    }
    
    // Get socket error
    static int get_socket_error(int fd) {
        int error = 0;
        socklen_t len = sizeof(error);
        getsockopt(fd, SOL_SOCKET, SO_ERROR, &error, &len);
        return error;
    }
};

// I/O vector builder for scatter/gather I/O
class IOVectorBuilder {
public:
    static constexpr size_t MAX_IOV = 1024;
    
    IOVectorBuilder() : count_(0) {
        iov_[0].iov_base = nullptr;
        iov_[0].iov_len = 0;
    }
    
    void add(void* base, size_t len) {
        if (count_ < MAX_IOV) {
            iov_[count_].iov_base = base;
            iov_[count_].iov_len = len;
            ++count_;
        }
    }
    
    void add(const std::string& str) {
        add(const_cast<char*>(str.data()), str.size());
    }
    
    iovec* iov() { return iov_; }
    size_t count() const { return count_; }
    
    void clear() {
        count_ = 0;
    }
    
private:
    iovec iov_[MAX_IOV];
    size_t count_;
};

// Batch message buffer for UDP multicast
class BatchMessageBuffer {
public:
    static constexpr size_t MAX_MESSAGES = 256;
    static constexpr size_t MAX_MESSAGE_SIZE = 65536;
    
    BatchMessageBuffer() {
        for (size_t i = 0; i < MAX_MESSAGES; ++i) {
            msgs_[i].msg_hdr.msg_iov = &iov_[i * 2];
            msgs_[i].msg_hdr.msg_iovlen = 1;
            
            iov_[i * 2].iov_base = buffers_[i];
            iov_[i * 2].iov_len = MAX_MESSAGE_SIZE;
        }
    }
    
    mmsghdr* msgs() { return msgs_; }
    size_t capacity() const { return MAX_MESSAGES; }
    
    void clear() {
        for (size_t i = 0; i < MAX_MESSAGES; ++i) {
            msgs_[i].msg_len = 0;
        }
    }
    
private:
    mmsghdr msgs_[MAX_MESSAGES];
    iovec iov_[MAX_MESSAGES * 2];
    char buffers_[MAX_MESSAGES][MAX_MESSAGE_SIZE];
};

} // namespace io
} // namespace best_server

#endif // BEST_SERVER_IO_OPTIMIZED_SOCKET_HPP