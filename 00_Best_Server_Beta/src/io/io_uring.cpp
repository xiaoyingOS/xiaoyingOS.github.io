// io_uring Implementation

#include "best_server/io/io_uring.hpp"
#include "best_server/core/lockfree_queue.hpp"
#include <cstring>
#include <stdexcept>
#include <system_error>

namespace best_server {
namespace io {

// Check if io_uring is available at compile time
#if defined(__linux__) && defined(HAVE_LIBURING)

// If liburing is available, include it
#include <liburing.h>

// Real io_uring implementation
class IoUringImpl {
public:
    IoUringImpl(const IoUringConfig& config) {
        memset(&params_, 0, sizeof(params_));
        params_.flags = IORING_SETUP_SQPOLL | IORING_SETUP_CQSIZE;
        params_.sq_thread_idle = config.sq_thread_idle;
        params_.sq_thread_cpu = config.sq_thread_cpu;
        params_.cq_entries = config.cq_entries;
        
        int ret = io_uring_queue_init_params(config.queue_entries, &ring_, &params_);
        if (ret < 0) {
            throw std::system_error(-ret, std::system_category(), "io_uring_queue_init_params");
        }
    }
    
    ~IoUringImpl() {
        io_uring_queue_exit(&ring_);
        
        // Clean up remaining callback data
        CallbackData* data = nullptr;
        while (callback_pool_.try_pop(data)) {
            delete data;
        }
    }
    
    bool submit_read(int fd, void* buffer, size_t count, off_t offset,
                    IoCallback callback, void* user_data) {
        struct io_uring_sqe* sqe = io_uring_get_sqe(&ring_);
        if (!sqe) {
            return false;
        }
        
        io_uring_prep_read(sqe, fd, buffer, count, offset);
        io_uring_sqe_set_data(sqe, acquire_callback(callback, user_data));
        
        return true;
    }
    
    bool submit_write(int fd, const void* buffer, size_t count, off_t offset,
                     IoCallback callback, void* user_data) {
        struct io_uring_sqe* sqe = io_uring_get_sqe(&ring_);
        if (!sqe) {
            return false;
        }
        
        io_uring_prep_write(sqe, fd, buffer, count, offset);
        io_uring_sqe_set_data(sqe, acquire_callback(callback, user_data));
        
        return true;
    }
    
    bool submit_accept(int fd, sockaddr* addr, socklen_t* addrlen,
                      IoCallback callback, void* user_data) {
        struct io_uring_sqe* sqe = io_uring_get_sqe(&ring_);
        if (!sqe) {
            return false;
        }
        
        io_uring_prep_accept(sqe, fd, addr, addrlen, 0);
        io_uring_sqe_set_data(sqe, acquire_callback(callback, user_data));
        
        return true;
    }
    
    bool submit_connect(int fd, const sockaddr* addr, socklen_t addrlen,
                       IoCallback callback, void* user_data) {
        struct io_uring_sqe* sqe = io_uring_get_sqe(&ring_);
        if (!sqe) {
            return false;
        }
        
        io_uring_prep_connect(sqe, fd, addr, addrlen);
        io_uring_sqe_set_data(sqe, acquire_callback(callback, user_data));
        
        return true;
    }
    
    bool submit_send(int fd, const void* buffer, size_t count, int flags,
                    IoCallback callback, void* user_data) {
        struct io_uring_sqe* sqe = io_uring_get_sqe(&ring_);
        if (!sqe) {
            return false;
        }
        
        io_uring_prep_send(sqe, fd, buffer, count, flags);
        io_uring_sqe_set_data(sqe, acquire_callback(callback, user_data));
        
        return true;
    }
    
    bool submit_recv(int fd, void* buffer, size_t count, int flags,
                    IoCallback callback, void* user_data) {
        struct io_uring_sqe* sqe = io_uring_get_sqe(&ring_);
        if (!sqe) {
            return false;
        }
        
        io_uring_prep_recv(sqe, fd, buffer, count, flags);
        io_uring_sqe_set_data(sqe, acquire_callback(callback, user_data));
        
        return true;
    }
    
    bool submit_close(int fd, IoCallback callback, void* user_data) {
        struct io_uring_sqe* sqe = io_uring_get_sqe(&ring_);
        if (!sqe) {
            return false;
        }
        
        io_uring_prep_close(sqe, fd);
        io_uring_sqe_set_data(sqe, acquire_callback(callback, user_data));
        
        return true;
    }
    
    size_t process_completions(size_t max_events) {
        struct io_uring_cqe* cqe;
        unsigned int head = 0;
        size_t count = 0;
        
        io_uring_for_each_cqe(&ring_, head, cqe) {
            if (count >= max_events) {
                break;
            }
            
            CallbackData* data = static_cast<CallbackData*>(io_uring_cqe_get_data(cqe));
            if (data) {
                IoResult result;
                result.result = cqe->res;
                result.error = cqe->res < 0 ? -cqe->res : 0;
                result.user_data = data->user_data;
                
                data->callback(result);
                release_callback(data);
            }
            
            io_uring_cqe_seen(&ring_, cqe);
            count++;
        }
        
        return count;
    }
    
    int wait_for_events(size_t min_events, int timeout_ms) {
        return io_uring_wait_cqe_timeout(&ring_, nullptr, timeout_ms);
    }
    
    int submit() {
        return io_uring_submit(&ring_);
    }
    
    io_uring& get_ring() { return ring_; }
    
private:
    struct CallbackData {
        IoCallback callback;
        void* user_data;
        
        CallbackData(IoCallback cb, void* ud) : callback(cb), user_data(ud) {}
    };
    
    // Acquire callback data from pool or allocate new
    CallbackData* acquire_callback(IoCallback callback, void* user_data) {
        CallbackData* data = nullptr;
        if (callback_pool_.try_pop(data)) {
            // Reuse from pool
            data->callback = callback;
            data->user_data = user_data;
        } else {
            // Allocate new
            data = new CallbackData(callback, user_data);
        }
        return data;
    }
    
    // Release callback data back to pool
    void release_callback(CallbackData* data) {
        if (!callback_pool_.try_push(data)) {
            // Pool full, delete
            delete data;
        }
    }
    
    io_uring ring_;
    struct io_uring_params params_;
    core::LockFreeQueue<CallbackData*, 256> callback_pool_;  // Object pool for callback data
};

#else

// Fallback implementation for systems without io_uring
class IoUringImpl {
public:
    IoUringImpl(const IoUringConfig&) {
        // Not supported
    }
    
    bool submit_read(int, void*, size_t, off_t, IoCallback, void*) { return false; }
    bool submit_write(int, const void*, size_t, off_t, IoCallback, void*) { return false; }
    bool submit_accept(int, sockaddr*, socklen_t*, IoCallback, void*) { return false; }
    bool submit_connect(int, const sockaddr*, socklen_t, IoCallback, void*) { return false; }
    bool submit_send(int, const void*, size_t, int, IoCallback, void*) { return false; }
    bool submit_recv(int, void*, size_t, int, IoCallback, void*) { return false; }
    bool submit_close(int, IoCallback, void*) { return false; }
    size_t process_completions(size_t) { return 0; }
    int wait_for_events(size_t, int) { return -ENOSYS; }
    int submit() { return -ENOSYS; }
};

#endif

// IoUring implementation
IoUring::IoUring() : initialized_(false) {}

IoUring::IoUring(const IoUringConfig& config) : initialized_(false) {
    initialize(config);
}

IoUring::~IoUring() {
    shutdown();
}

bool IoUring::is_available() const {
#if defined(__linux__) && defined(HAVE_LIBURING)
    return true;
#else
    return false;
#endif
}

bool IoUring::initialize(const IoUringConfig& config) {
    if (!is_available()) {
        return false;
    }
    
    try {
        impl_ = std::make_unique<IoUringImpl>(config);
        initialized_ = true;
        return true;
    } catch (...) {
        initialized_ = false;
        return false;
    }
}

void IoUring::shutdown() {
    impl_.reset();
    initialized_ = false;
}

bool IoUring::submit_read(int fd, void* buffer, size_t count, off_t offset,
                         IoCallback callback, void* user_data) {
    if (!initialized_ || !impl_) return false;
    return impl_->submit_read(fd, buffer, count, offset, callback, user_data);
}

bool IoUring::submit_write(int fd, const void* buffer, size_t count, off_t offset,
                          IoCallback callback, void* user_data) {
    if (!initialized_ || !impl_) return false;
    return impl_->submit_write(fd, buffer, count, offset, callback, user_data);
}

bool IoUring::submit_accept(int fd, sockaddr* addr, socklen_t* addrlen,
                           IoCallback callback, void* user_data) {
    if (!initialized_ || !impl_) return false;
    return impl_->submit_accept(fd, addr, addrlen, callback, user_data);
}

bool IoUring::submit_connect(int fd, const sockaddr* addr, socklen_t addrlen,
                            IoCallback callback, void* user_data) {
    if (!initialized_ || !impl_) return false;
    return impl_->submit_connect(fd, addr, addrlen, callback, user_data);
}

bool IoUring::submit_send(int fd, const void* buffer, size_t count, int flags,
                         IoCallback callback, void* user_data) {
    if (!initialized_ || !impl_) return false;
    return impl_->submit_send(fd, buffer, count, flags, callback, user_data);
}

bool IoUring::submit_recv(int fd, void* buffer, size_t count, int flags,
                         IoCallback callback, void* user_data) {
    if (!initialized_ || !impl_) return false;
    return impl_->submit_recv(fd, buffer, count, flags, callback, user_data);
}

bool IoUring::submit_close(int fd, IoCallback callback, void* user_data) {
    if (!initialized_ || !impl_) return false;
    return impl_->submit_close(fd, callback, user_data);
}

size_t IoUring::process_completions(size_t max_events) {
    if (!initialized_ || !impl_) return 0;
    return impl_->process_completions(max_events);
}

int IoUring::wait_for_events(size_t min_events, int timeout_ms) {
    if (!initialized_ || !impl_) return -ENOSYS;
    return impl_->wait_for_events(min_events, timeout_ms);
}

size_t IoUring::pending_submissions() const {
    // Implementation would return pending submission count
    return 0;
}

size_t IoUring::pending_completions() const {
    // Implementation would return pending completion count
    return 0;
}

int IoUring::submit() {
    if (!initialized_ || !impl_) return -ENOSYS;
    return impl_->submit();
}

IoUring::Stats IoUring::get_stats() const {
    Stats stats{};
    // Implementation would fill in actual stats
    return stats;
}

// IoUringFile implementation
IoUringFile::IoUringFile(IoUring* io_uring, int fd)
    : io_uring_(io_uring), fd_(fd) {}

IoUringFile::~IoUringFile() {}

bool IoUringFile::read(void* buffer, size_t count, off_t offset,
                       IoCallback callback, void* user_data) {
    return io_uring_->submit_read(fd_, buffer, count, offset, callback, user_data);
}

bool IoUringFile::write(const void* buffer, size_t count, off_t offset,
                        IoCallback callback, void* user_data) {
    return io_uring_->submit_write(fd_, buffer, count, offset, callback, user_data);
}

bool IoUringFile::close(IoCallback callback, void* user_data) {
    return io_uring_->submit_close(fd_, callback, user_data);
}

// IoUringSocket implementation
IoUringSocket::IoUringSocket(IoUring* io_uring, int fd)
    : io_uring_(io_uring), fd_(fd) {}

IoUringSocket::~IoUringSocket() {}

bool IoUringSocket::accept(sockaddr* addr, socklen_t* addrlen,
                          IoCallback callback, void* user_data) {
    return io_uring_->submit_accept(fd_, addr, addrlen, callback, user_data);
}

bool IoUringSocket::connect(const sockaddr* addr, socklen_t addrlen,
                            IoCallback callback, void* user_data) {
    return io_uring_->submit_connect(fd_, addr, addrlen, callback, user_data);
}

bool IoUringSocket::send(const void* buffer, size_t count, int flags,
                         IoCallback callback, void* user_data) {
    return io_uring_->submit_send(fd_, buffer, count, flags, callback, user_data);
}

bool IoUringSocket::recv(void* buffer, size_t count, int flags,
                         IoCallback callback, void* user_data) {
    return io_uring_->submit_recv(fd_, buffer, count, flags, callback, user_data);
}

bool IoUringSocket::close(IoCallback callback, void* user_data) {
    return io_uring_->submit_close(fd_, callback, user_data);
}

// IoUringFactory implementation
std::unique_ptr<IoUring> IoUringFactory::create(const IoUringConfig& config) {
    auto io_uring = std::make_unique<IoUring>();
    if (!io_uring->initialize(config)) {
        return nullptr;
    }
    return io_uring;
}

bool IoUringFactory::is_supported() {
#if defined(__linux__) && defined(HAVE_LIBURING)
    return true;
#else
    return false;
#endif
}

int IoUringFactory::get_version() {
#if defined(__linux__) && defined(HAVE_LIBURING)
    return io_uring_get_version();
#else
    return 0;
#endif
}

uint32_t IoUringFactory::get_features() {
#if defined(__linux__) && defined(HAVE_LIBURING)
    return io_uring_get_features();
#else
    return 0;
#endif
}

} // namespace io
} // namespace best_server