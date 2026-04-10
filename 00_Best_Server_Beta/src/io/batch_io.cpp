// BatchIO implementation

#include "best_server/io/batch_io.hpp"
#include <unistd.h>
#include <fcntl.h>
#include <cerrno>
#include <cstring>

namespace best_server {
namespace io {

// ==================== BatchIO Implementation ====================

BatchIO::BatchIO()
    : batch_size_(32)
    , recv_msgvec_(nullptr)
    , recv_iov_(nullptr)
    , send_msgvec_(nullptr)
    , send_iov_(nullptr)
{
    init_message_headers();
}

BatchIO::~BatchIO() {
    cleanup_message_headers();
}

void BatchIO::init_message_headers() {
    recv_msgvec_ = new struct mmsghdr[batch_size_];
    recv_iov_ = new struct iovec[batch_size_];
    send_msgvec_ = new struct mmsghdr[batch_size_];
    send_iov_ = new struct iovec[batch_size_];
    
    // Initialize message headers
    memset(recv_msgvec_, 0, sizeof(struct mmsghdr) * batch_size_);
    memset(send_msgvec_, 0, sizeof(struct mmsghdr) * batch_size_);
    
    for (size_t i = 0; i < batch_size_; ++i) {
        recv_msgvec_[i].msg_hdr.msg_iov = &recv_iov_[i];
        recv_msgvec_[i].msg_hdr.msg_iovlen = 1;
        
        send_msgvec_[i].msg_hdr.msg_iov = &send_iov_[i];
        send_msgvec_[i].msg_hdr.msg_iovlen = 1;
    }
}

void BatchIO::cleanup_message_headers() {
    delete[] recv_msgvec_;
    delete[] recv_iov_;
    delete[] send_msgvec_;
    delete[] send_iov_;
}

bool BatchIO::set_batch_size(size_t size) {
    if (size > MAX_BATCH_SIZE || (size & (size - 1)) != 0) {
        return false; // Must be power of 2 and <= MAX_BATCH_SIZE
    }
    
    if (size != batch_size_) {
        cleanup_message_headers();
        batch_size_ = size;
        init_message_headers();
    }
    
    return true;
}

BatchRecvResult BatchIO::recv_batch(int sockfd, std::vector<memory::ZeroCopyBuffer>& buffers) {
    BatchRecvResult result;
    result.count = 0;
    
    if (buffers.size() > batch_size_) {
        result.count = 0;
        return result;
    }
    
    // Prepare message headers
    for (size_t i = 0; i < buffers.size(); ++i) {
        recv_iov_[i].iov_base = buffers[i].data();
        recv_iov_[i].iov_len = buffers[i].capacity();
        recv_msgvec_[i].msg_hdr.msg_iov = &recv_iov_[i];
        recv_msgvec_[i].msg_hdr.msg_iovlen = 1;
    }
    
    // Receive messages
    int nrecv = recvmmsg(sockfd, recv_msgvec_, buffers.size(), 
                        MSG_DONTWAIT | MSG_NOSIGNAL, nullptr);
    
    if (nrecv > 0) {
        result.count = static_cast<size_t>(nrecv);
        result.sizes.resize(result.count);
        result.buffers.resize(result.count);
        
        for (size_t i = 0; i < result.count; ++i) {
            result.sizes[i] = recv_msgvec_[i].msg_len;
            result.buffers[i] = buffers[i].data();
        }
        
        // Update buffer sizes (note: buffer sizes may need manual adjustment)
        // For now, we just record the received sizes in result.sizes
    } else if (nrecv < 0 && errno != EAGAIN && errno != EWOULDBLOCK) {
        // Error occurred
    }
    
    return result;
}

BatchSendResult BatchIO::send_batch(int sockfd, const std::vector<memory::ZeroCopyBuffer>& buffers) {
    BatchSendResult result;
    result.count = 0;
    result.total_bytes = 0;
    
    if (buffers.empty() || buffers.size() > batch_size_) {
        return result;
    }
    
    // Prepare message headers
    for (size_t i = 0; i < buffers.size(); ++i) {
        send_iov_[i].iov_base = const_cast<char*>(buffers[i].data());
        send_iov_[i].iov_len = buffers[i].size();
        send_msgvec_[i].msg_hdr.msg_iov = &send_iov_[i];
        send_msgvec_[i].msg_hdr.msg_iovlen = 1;
    }
    
    // Send messages
    int nsent = sendmmsg(sockfd, send_msgvec_, buffers.size(), MSG_NOSIGNAL);
    
    if (nsent > 0) {
        result.count = static_cast<size_t>(nsent);
        
        for (size_t i = 0; i < result.count; ++i) {
            if (send_msgvec_[i].msg_len >= 0) {
                result.total_bytes += static_cast<size_t>(send_msgvec_[i].msg_len);
            } else {
                result.errors.push_back(errno);
            }
        }
    } else if (nsent < 0 && errno != EAGAIN && errno != EWOULDBLOCK) {
        // Error occurred
    }
    
    return result;
}

ssize_t BatchIO::readv(int sockfd, const std::vector<iovec>& iov) {
    if (iov.empty() || iov.size() > IOV_MAX) {
        return -1;
    }
    
    // Use stack allocation for small operations, heap for large ones
    struct iovec* local_iov;
    struct iovec stack_iov[MAX_STACK_IOV];
    
    if (iov.size() <= MAX_STACK_IOV) {
        // Stack allocation - fast
        local_iov = stack_iov;
    } else {
        // Heap allocation for large operations
        local_iov = new struct iovec[iov.size()];
    }
    
    memcpy(local_iov, iov.data(), sizeof(struct iovec) * iov.size());
    
    ssize_t bytes_read = ::readv(sockfd, local_iov, iov.size());
    
    // Only delete if heap allocated
    if (iov.size() > MAX_STACK_IOV) {
        delete[] local_iov;
    }
    
    return bytes_read;
}

ssize_t BatchIO::writev(int sockfd, const std::vector<iovec>& iov) {
    if (iov.empty() || iov.size() > IOV_MAX) {
        return -1;
    }
    
    // Use stack allocation for small operations, heap for large ones
    struct iovec* local_iov;
    struct iovec stack_iov[MAX_STACK_IOV];
    
    if (iov.size() <= MAX_STACK_IOV) {
        // Stack allocation - fast
        local_iov = stack_iov;
    } else {
        // Heap allocation for large operations
        local_iov = new struct iovec[iov.size()];
    }
    
    memcpy(local_iov, iov.data(), sizeof(struct iovec) * iov.size());
    
    ssize_t bytes_written = ::writev(sockfd, local_iov, iov.size());
    
    // Only delete if heap allocated
    if (iov.size() > MAX_STACK_IOV) {
        delete[] local_iov;
    }
    
    return bytes_written;
}

// ==================== BatchReceiver Implementation ====================

BatchReceiver::BatchReceiver(size_t buffer_size, size_t pool_size)
    : buffer_size_(buffer_size)
    , pool_index_(0)
    , stats_{0, 0, 0, 0}
{
    buffer_pool_.reserve(pool_size);
    
    // Pre-allocate buffers
    for (size_t i = 0; i < pool_size; ++i) {
        buffer_pool_.push_back(new memory::ZeroCopyBuffer(buffer_size));
    }
}

BatchReceiver::~BatchReceiver() {
    // Free all buffers
    for (auto* buffer : buffer_pool_) {
        delete buffer;
    }
    buffer_pool_.clear();
}

BatchRecvResult BatchReceiver::receive(int sockfd) {
    std::vector<memory::ZeroCopyBuffer*> buffers;
    
    // Get buffers from pool
    size_t max_buffers = std::min(buffer_pool_.size(), batch_io_.batch_size());
    for (size_t i = 0; i < max_buffers; ++i) {
        buffers.push_back(get_buffer());
    }
    
    // Convert to vector for batch_io
    std::vector<memory::ZeroCopyBuffer> buffer_vec;
    for (auto* buf : buffers) {
        buffer_vec.push_back(*buf);
    }
    
    // Receive batch
    BatchRecvResult result = batch_io_.recv_batch(sockfd, buffer_vec);
    
    // Update statistics
    stats_.total_received += result.count;
    for (size_t i = 0; i < result.sizes.size(); ++i) {
        stats_.total_bytes += result.sizes[i];
    }
    
    // Return used buffers to pool
    if (result.count < buffers.size()) {
        // Unused buffers go back to pool
        for (size_t i = result.count; i < buffers.size(); ++i) {
            return_buffer(buffers[i]);
        }
    }
    
    return result;
}

void BatchReceiver::return_buffers(const std::vector<void*>& buffers) {
    for (void* ptr : buffers) {
        for (size_t i = 0; i < buffer_pool_.size(); ++i) {
            if (buffer_pool_[i]->data() == ptr) {
                buffer_pool_[i]->clear();
                return;
            }
        }
    }
}

memory::ZeroCopyBuffer* BatchReceiver::get_buffer() {
    if (buffer_pool_.empty()) {
        // Pool exhausted, allocate new buffer
        stats_.pool_misses++;
        return new memory::ZeroCopyBuffer(buffer_size_);
    }
    
    // Get from pool
    memory::ZeroCopyBuffer* buffer = buffer_pool_.back();
    buffer_pool_.pop_back();
    buffer->clear();
    stats_.pool_hits++;
    return buffer;
}

void BatchReceiver::return_buffer(memory::ZeroCopyBuffer* buffer) {
    if (buffer_pool_.size() < buffer_pool_.capacity()) {
        buffer_pool_.push_back(buffer);
    } else {
        delete buffer;
    }
}

// ==================== BatchSender Implementation ====================

BatchSender::BatchSender(size_t batch_size)
    : stats_{0, 0, 0, 0}
{
    batch_io_.set_batch_size(batch_size);
    send_queue_.reserve(batch_size);
}

BatchSender::~BatchSender() {
    // Send any remaining queued buffers
    if (!send_queue_.empty()) {
        // Note: This requires a socket fd, which we don't have here
        // In real usage, the owner should call send() before destruction
    }
}

void BatchSender::queue(memory::ZeroCopyBuffer&& buffer) {
    send_queue_.push_back(std::move(buffer));
    
    if (send_queue_.size() > send_queue_.capacity()) {
        stats_.queue_overflows++;
    }
}

BatchSendResult BatchSender::send(int sockfd) {
    BatchSendResult result;
    
    if (send_queue_.empty()) {
        return result;
    }
    
    result = batch_io_.send_batch(sockfd, send_queue_);
    
    // Update statistics
    stats_.total_sent += result.count;
    stats_.total_bytes += result.total_bytes;
    stats_.batches_sent++;
    
    // Remove sent buffers
    send_queue_.erase(send_queue_.begin(), 
                      send_queue_.begin() + result.count);
    
    return result;
}

// ==================== ZeroCopyTransfer Implementation ====================

int ZeroCopyTransfer::create_pipe() {
    int pipefd[2];
    if (pipe(pipefd) == -1) {
        return -1;
    }
    
    // Set pipe size
    fcntl(pipefd[0], F_SETPIPE_SZ, SPLICE_PIPE_SIZE);
    fcntl(pipefd[1], F_SETPIPE_SZ, SPLICE_PIPE_SIZE);
    
    // Make pipe non-blocking
    fcntl(pipefd[0], F_SETFL, O_NONBLOCK);
    fcntl(pipefd[1], F_SETFL, O_NONBLOCK);
    
    return pipefd[0]; // Return read end, caller manages both
}

ssize_t ZeroCopyTransfer::file_to_socket(int file_fd, int sock_fd,
                                         off_t* offset, size_t count) {
    int pipefd[2];
    if (pipe(pipefd) == -1) {
        return -1;
    }
    
    fcntl(pipefd[0], F_SETPIPE_SZ, SPLICE_PIPE_SIZE);
    fcntl(pipefd[1], F_SETPIPE_SZ, SPLICE_PIPE_SIZE);
    
    ssize_t total = 0;
    
    while (count > 0) {
        // Copy from file to pipe
        ssize_t copied = splice(file_fd, offset, pipefd[1], nullptr,
                              std::min(count, SPLICE_PIPE_SIZE),
                              SPLICE_F_MOVE | SPLICE_F_MORE);
        
        if (copied <= 0) {
            break;
        }
        
        // Copy from pipe to socket
        ssize_t sent = splice(pipefd[0], nullptr, sock_fd, nullptr,
                             copied, SPLICE_F_MOVE | SPLICE_F_MORE);
        
        if (sent <= 0) {
            break;
        }
        
        total += sent;
        count -= sent;
    }
    
    close(pipefd[0]);
    close(pipefd[1]);
    
    return total;
}

ssize_t ZeroCopyTransfer::socket_to_file(int sock_fd, int file_fd,
                                         off_t* offset, size_t count) {
    int pipefd[2];
    if (pipe(pipefd) == -1) {
        return -1;
    }
    
    fcntl(pipefd[0], F_SETPIPE_SZ, SPLICE_PIPE_SIZE);
    fcntl(pipefd[1], F_SETPIPE_SZ, SPLICE_PIPE_SIZE);
    
    ssize_t total = 0;
    
    while (count > 0) {
        // Copy from socket to pipe
        ssize_t copied = splice(sock_fd, nullptr, pipefd[1], nullptr,
                              std::min(count, SPLICE_PIPE_SIZE),
                              SPLICE_F_MOVE | SPLICE_F_MORE);
        
        if (copied <= 0) {
            break;
        }
        
        // Copy from pipe to file
        ssize_t written = splice(pipefd[0], nullptr, file_fd, offset,
                                 copied, SPLICE_F_MOVE | SPLICE_F_MORE);
        
        if (written <= 0) {
            break;
        }
        
        total += written;
        count -= written;
    }
    
    close(pipefd[0]);
    close(pipefd[1]);
    
    return total;
}

} // namespace io
} // namespace best_server