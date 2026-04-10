// BatchIO - High-performance batch I/O operations
// 
// Implements batch I/O operations using recvmmsg/sendmmsg
// to reduce system call overhead and improve throughput.
//
// Features:
// - recvmmsg/sendmmsg support
// - Zero-copy buffer support
// - Vectorized I/O
// - Reduced syscalls

#ifndef BEST_SERVER_IO_BATCH_IO_HPP
#define BEST_SERVER_IO_BATCH_IO_HPP

#include <best_server/memory/zero_copy_buffer.hpp>
#include <vector>
#include <array>
#include <cstdint>
#include <cstring>
#include <sys/socket.h>
#include <sys/uio.h>

namespace best_server {
namespace io {

// Maximum batch size for recvmmsg/sendmmsg
constexpr size_t MAX_BATCH_SIZE = 64;

// Batch receive result
struct BatchRecvResult {
    size_t count;              // Number of messages received
    std::vector<size_t> sizes;  // Size of each message
    std::vector<void*> buffers; // Buffer pointers
};

// Batch send result
struct BatchSendResult {
    size_t count;      // Number of messages sent
    size_t total_bytes; // Total bytes sent
    std::vector<int> errors; // Error codes for each message
};

// Batch I/O operations
class BatchIO {
public:
    BatchIO();
    ~BatchIO();
    
    // Receive multiple messages in one system call
    BatchRecvResult recv_batch(int sockfd, std::vector<memory::ZeroCopyBuffer>& buffers);
    
    // Send multiple messages in one system call
    BatchSendResult send_batch(int sockfd, const std::vector<memory::ZeroCopyBuffer>& buffers);
    
    // Vectorized read (scatter I/O)
    ssize_t readv(int sockfd, const std::vector<iovec>& iov);
    
    // Vectorized write (gather I/O)
    ssize_t writev(int sockfd, const std::vector<iovec>& iov);
    
    // Set batch size (must be power of 2 and <= MAX_BATCH_SIZE)
    bool set_batch_size(size_t size);
    
    size_t batch_size() const { return batch_size_; }
    
private:
    void init_message_headers();
    void cleanup_message_headers();
    
    size_t batch_size_;
    
    // Pre-allocated message headers
    struct mmsghdr* recv_msgvec_;
    struct iovec* recv_iov_;
    
    struct mmsghdr* send_msgvec_;
    struct iovec* send_iov_;
    
    // Stack-allocated iovec for small operations (optimization)
    static constexpr size_t MAX_STACK_IOV = 32;
};

// Optimized batch receiver with buffer pool
class BatchReceiver {
public:
    explicit BatchReceiver(size_t buffer_size = 65536, size_t pool_size = 1024);
    ~BatchReceiver();
    
    // Receive batch and return buffers
    BatchRecvResult receive(int sockfd);
    
    // Return buffers to pool
    void return_buffers(const std::vector<void*>& buffers);
    
    // Get statistics
    struct Stats {
        uint64_t total_received;
        uint64_t total_bytes;
        uint64_t pool_hits;
        uint64_t pool_misses;
    };
    
    Stats get_stats() const { return stats_; }
    
private:
    memory::ZeroCopyBuffer* get_buffer();
    void return_buffer(memory::ZeroCopyBuffer* buffer);
    
    const size_t buffer_size_;
    std::vector<memory::ZeroCopyBuffer*> buffer_pool_;
    [[maybe_unused]] size_t pool_index_;
    BatchIO batch_io_;
    Stats stats_;
};

// Optimized batch sender with buffer pool
class BatchSender {
public:
    explicit BatchSender(size_t batch_size = 64);
    ~BatchSender();
    
    // Queue buffer for sending
    void queue(memory::ZeroCopyBuffer&& buffer);
    
    // Send queued buffers in batch
    BatchSendResult send(int sockfd);
    
    // Get statistics
    struct Stats {
        uint64_t total_sent;
        uint64_t total_bytes;
        uint64_t batches_sent;
        uint64_t queue_overflows;
    };
    
    Stats get_stats() const { return stats_; }
    
    size_t queue_size() const { return send_queue_.size(); }
    
private:
    std::vector<memory::ZeroCopyBuffer> send_queue_;
    BatchIO batch_io_;
    Stats stats_;
};

// Helper functions for batch I/O

// Prepare iovec from ZeroCopyBuffer
inline void prepare_iovec(const memory::ZeroCopyBuffer& buffer, iovec& iov) {
    iov.iov_base = const_cast<char*>(buffer.data());
    iov.iov_len = buffer.size();
}

// Prepare multiple iovec from buffer vector
inline void prepare_iovec(const std::vector<memory::ZeroCopyBuffer>& buffers, 
                         iovec* iov, size_t count) {
    size_t n = std::min(count, buffers.size());
    for (size_t i = 0; i < n; ++i) {
        prepare_iovec(buffers[i], iov[i]);
    }
}

// Zero-copy file transfer using splice
class ZeroCopyTransfer {
public:
    // Transfer data from file descriptor to socket
    static ssize_t file_to_socket(int file_fd, int sock_fd, 
                                   off_t* offset, size_t count);
    
    // Transfer data from socket to file descriptor
    static ssize_t socket_to_file(int sock_fd, int file_fd,
                                   off_t* offset, size_t count);
    
    // Pipe buffer size for splice
    static constexpr size_t SPLICE_PIPE_SIZE = 64 * 1024;
    
private:
    static int create_pipe();
};

} // namespace io
} // namespace best_server

#endif // BEST_SERVER_IO_BATCH_IO_HPP