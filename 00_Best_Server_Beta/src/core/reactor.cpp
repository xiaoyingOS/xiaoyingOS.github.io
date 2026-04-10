// Reactor - I/O event loop implementation

#include "best_server/core/reactor.hpp"
#include <unistd.h>
#include <fcntl.h>
#include <cstring>
#include <ctime>

namespace best_server {
namespace core {

// Reactor implementation
Reactor::Reactor() {
    // Pre-allocate fd_map_ with enough buckets to reduce rehashing
    fd_map_.reserve(1024);  // Pre-allocate for better performance
    
    // Create wake-up pipe
#if BEST_SERVER_PLATFORM_LINUX
    epoll_fd_ = epoll_create1(EPOLL_CLOEXEC);
    epoll_events_ = new epoll_event[MAX_EVENTS];
    
    pipe(wake_fd_);
    fcntl(wake_fd_[0], F_SETFL, O_NONBLOCK);
    fcntl(wake_fd_[1], F_SETFL, O_NONBLOCK);
    
    struct epoll_event ev;
    ev.events = EPOLLIN | EPOLLET;
    ev.data.fd = wake_fd_[0];
    epoll_ctl(epoll_fd_, EPOLL_CTL_ADD, wake_fd_[0], &ev);
    
#elif BEST_SERVER_PLATFORM_MACOS
    kqueue_fd_ = kqueue();
    kqueue_events_ = new kevent[MAX_EVENTS];
    
    pipe(wake_fd_);
    fcntl(wake_fd_[0], F_SETFL, O_NONBLOCK);
    fcntl(wake_fd_[1], F_SETFL, O_NONBLOCK);
    
    struct kevent ev;
    EV_SET(&ev, wake_fd_[0], EVFILT_READ, EV_ADD | EV_CLEAR, 0, 0, nullptr);
    kevent(kqueue_fd_, &ev, 1, nullptr, 0, nullptr);
    
#elif BEST_SERVER_PLATFORM_WINDOWS
    iocp_port_ = CreateIoCompletionPort(INVALID_HANDLE_VALUE, nullptr, 0, 0);
    iocp_events_ = new OVERLAPPED_ENTRY[MAX_EVENTS];
    
    // Create event for wake-up
    wake_fd_[0] = CreateEvent(nullptr, FALSE, FALSE, nullptr);
    wake_fd_[1] = wake_fd_[0]; // Windows uses different mechanism
#endif
    
    update_time();
}

Reactor::~Reactor() {
    stop();
    
#if BEST_SERVER_PLATFORM_LINUX
    if (epoll_fd_ != -1) {
        close(epoll_fd_);
    }
    delete[] epoll_events_;
    close(wake_fd_[0]);
    close(wake_fd_[1]);
    
#elif BEST_SERVER_PLATFORM_MACOS
    if (kqueue_fd_ != -1) {
        close(kqueue_fd_);
    }
    delete[] kqueue_events_;
    close(wake_fd_[0]);
    close(wake_fd_[1]);
    
#elif BEST_SERVER_PLATFORM_WINDOWS
    if (iocp_port_ != INVALID_HANDLE_VALUE) {
        CloseHandle(iocp_port_);
    }
    delete[] iocp_events_;
    CloseHandle(wake_fd_[0]);
#endif
}

void Reactor::run() {
    running_.store(true);
    
    while (running_.load()) {
        poll_events(100);
        update_time();
    }
}

void Reactor::stop() {
    if (!running_.load()) {
        return;
    }
    
    running_.store(false);
    wake_up();
}

bool Reactor::register_fd(int fd, IOEventType events, IOEventHandler handler) {
    FDRegistration reg;
    reg.fd = fd;
    reg.handler = std::move(handler);
    reg.events = events;
    reg.user_data = nullptr;
    
    // Set non-blocking
#if !BEST_SERVER_PLATFORM_WINDOWS
    int flags = fcntl(fd, F_GETFL, 0);
    fcntl(fd, F_SETFL, flags | O_NONBLOCK);
#endif
    
#if BEST_SERVER_PLATFORM_LINUX
    struct epoll_event ev;
    ev.events = EPOLLET; // Edge-triggered
    if (events & IOEventType::Read) ev.events |= EPOLLIN;
    if (events & IOEventType::Write) ev.events |= EPOLLOUT;
    ev.data.fd = fd;
    
    if (epoll_ctl(epoll_fd_, EPOLL_CTL_ADD, fd, &ev) == -1) {
        return false;
    }
    
#elif BEST_SERVER_PLATFORM_MACOS
    struct kevent ev[2];
    int nev = 0;
    
    if (events & IOEventType::Read) {
        EV_SET(&ev[nev++], fd, EVFILT_READ, EV_ADD | EV_CLEAR, 0, 0, nullptr);
    }
    if (events & IOEventType::Write) {
        EV_SET(&ev[nev++], fd, EVFILT_WRITE, EV_ADD | EV_CLEAR, 0, 0, nullptr);
    }
    
    if (kevent(kqueue_fd_, ev, nev, nullptr, 0, nullptr) == -1) {
        return false;
    }
    
#elif BEST_SERVER_PLATFORM_WINDOWS
    // Windows IOCP registration
    HANDLE handle = reinterpret_cast<HANDLE>(fd);
    CreateIoCompletionPort(handle, iocp_port_, fd, 0);
#endif
    
    fd_map_[fd] = std::move(reg);
    ++stats_.registered_fds;
    
    return true;
}

bool Reactor::modify_fd(int fd, IOEventType events) {
    auto it = fd_map_.find(fd);
    if (it == fd_map_.end()) {
        return false;
    }
    
    it->second.events = events;
    
#if BEST_SERVER_PLATFORM_LINUX
    struct epoll_event ev;
    ev.events = EPOLLET;
    if (events & IOEventType::Read) ev.events |= EPOLLIN;
    if (events & IOEventType::Write) ev.events |= EPOLLOUT;
    ev.data.fd = fd;
    
    return epoll_ctl(epoll_fd_, EPOLL_CTL_MOD, fd, &ev) != -1;
    
#elif BEST_SERVER_PLATFORM_MACOS
    // Remove old filters
    struct kevent ev[2];
    int nev = 0;
    EV_SET(&ev[nev++], fd, EVFILT_READ, EV_DELETE, 0, 0, nullptr);
    EV_SET(&ev[nev++], fd, EVFILT_WRITE, EV_DELETE, 0, 0, nullptr);
    kevent(kqueue_fd_, ev, nev, nullptr, 0, nullptr);
    
    // Add new filters
    nev = 0;
    if (events & IOEventType::Read) {
        EV_SET(&ev[nev++], fd, EVFILT_READ, EV_ADD | EV_CLEAR, 0, 0, nullptr);
    }
    if (events & IOEventType::Write) {
        EV_SET(&ev[nev++], fd, EVFILT_WRITE, EV_ADD | EV_CLEAR, 0, 0, nullptr);
    }
    
    return kevent(kqueue_fd_, ev, nev, nullptr, 0, nullptr) != -1;
    
#elif BEST_SERVER_PLATFORM_WINDOWS
    // IOCP doesn't need explicit modification
    return true;
#else
    return false;
#endif
}

bool Reactor::unregister_fd(int fd) {
    auto it = fd_map_.find(fd);
    if (it == fd_map_.end()) {
        return false;
    }
    
#if BEST_SERVER_PLATFORM_LINUX
    epoll_ctl(epoll_fd_, EPOLL_CTL_DEL, fd, nullptr);
#elif BEST_SERVER_PLATFORM_MACOS
    struct kevent ev[2];
    EV_SET(&ev[0], fd, EVFILT_READ, EV_DELETE, 0, 0, nullptr);
    EV_SET(&ev[1], fd, EVFILT_WRITE, EV_DELETE, 0, 0, nullptr);
    kevent(kqueue_fd_, ev, 2, nullptr, 0, nullptr);
#elif BEST_SERVER_PLATFORM_WINDOWS
    // IOCP cleanup happens automatically
#endif
    
    fd_map_.erase(it);
    --stats_.registered_fds;
    
    return true;
}

void Reactor::wake_up() {
#if BEST_SERVER_PLATFORM_LINUX || BEST_SERVER_PLATFORM_MACOS
    char c = 1;
    write(wake_fd_[1], &c, 1);
#elif BEST_SERVER_PLATFORM_WINDOWS
    SetEvent(wake_fd_[0]);
#endif
    ++stats_.wake_count;
}

bool Reactor::run_once(int timeout_ms) {
    poll_events(timeout_ms);
    update_time();
    return running_.load();
}

bool Reactor::poll_events(int timeout_ms) {
    (void)timeout_ms;  // Suppress unused parameter warning - used in platform-specific code
    ++stats_.poll_count;
    
#if BEST_SERVER_PLATFORM_LINUX
    int nfds = epoll_wait(epoll_fd_, epoll_events_, MAX_EVENTS, timeout_ms);
    if (nfds == -1) {
        if (errno == EINTR) return true;
        return false;
    }
    
    if (nfds == 0) {
        ++stats_.idle_count;
        return true;
    }
    
    ++stats_.events_processed;
    
    for (int i = 0; i < nfds; ++i) {
        int fd = epoll_events_[i].data.fd;
        
        // Handle wake-up pipe
        if (fd == wake_fd_[0]) {
            char buf[256];
            read(fd, buf, sizeof(buf));
            continue;
        }
        
        auto it = fd_map_.find(fd);
        if (it != fd_map_.end()) {
            IOEventType events = IOEventType::None;
            if (epoll_events_[i].events & EPOLLIN) events |= IOEventType::Read;
            if (epoll_events_[i].events & EPOLLOUT) events |= IOEventType::Write;
            if (epoll_events_[i].events & EPOLLERR) events |= IOEventType::Error;
            if (epoll_events_[i].events & EPOLLHUP) events |= IOEventType::Hangup;
            
            ++stats_.events_dispatched;
            it->second.handler(events, fd);
        }
    }
    
    return true;
    
#elif BEST_SERVER_PLATFORM_MACOS
    struct timespec timeout;
    timeout.tv_sec = timeout_ms / 1000;
    timeout.tv_nsec = (timeout_ms % 1000) * 1000000;
    
    int nev = kevent(kqueue_fd_, nullptr, 0, kqueue_events_, MAX_EVENTS, &timeout);
    if (nev == -1) {
        if (errno == EINTR) return true;
        return false;
    }
    
    if (nev == 0) {
        ++stats_.idle_count;
        return true;
    }
    
    ++stats_.events_processed;
    
    for (int i = 0; i < nev; ++i) {
        int fd = kqueue_events_[i].ident;
        
        // Handle wake-up pipe
        if (fd == wake_fd_[0]) {
            char buf[256];
            read(fd, buf, sizeof(buf));
            continue;
        }
        
        auto it = fd_map_.find(fd);
        if (it != fd_map_.end()) {
            IOEventType events = IOEventType::None;
            if (kqueue_events_[i].filter == EVFILT_READ) events |= IOEventType::Read;
            if (kqueue_events_[i].filter == EVFILT_WRITE) events |= IOEventType::Write;
            if (kqueue_events_[i].flags & EV_ERROR) events |= IOEventType::Error;
            
            ++stats_.events_dispatched;
            it->second.handler(events, fd);
        }
    }
    
    return true;
    
#elif BEST_SERVER_PLATFORM_WINDOWS
    DWORD bytes_transferred;
    ULONG_PTR completion_key;
    LPOVERLAPPED overlapped;
    
    BOOL success = GetQueuedCompletionStatus(
        iocp_port_, &bytes_transferred, &completion_key,
        &overlapped, timeout_ms
    );
    
    if (!success && !overlapped) {
        if (GetLastError() == WAIT_TIMEOUT) {
            ++stats_.idle_count;
            return true;
        }
        return false;
    }
    
    ++stats_.events_processed;
    ++stats_.events_dispatched;
    
    int fd = static_cast<int>(completion_key);
    auto it = fd_map_.find(fd);
    if (it != fd_map_.end()) {
        IOEventType events = success ? IOEventType::Read : IOEventType::Error;
        it->second.handler(events, fd);
    }
    
    return true;
#else
    return false;
#endif
}

void Reactor::update_time() {
#if BEST_SERVER_PLATFORM_LINUX || BEST_SERVER_PLATFORM_MACOS
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    current_time_ms_ = ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
#elif BEST_SERVER_PLATFORM_WINDOWS
    LARGE_INTEGER frequency, counter;
    QueryPerformanceFrequency(&frequency);
    QueryPerformanceCounter(&counter);
    current_time_ms_ = static_cast<uint64_t>(counter.QuadPart * 1000.0 / frequency.QuadPart);
#endif
}

// IOBuffer implementation
IOBuffer::IOBuffer(size_t capacity)
    : data_(new char[capacity])
    , capacity_(capacity)
    , size_(0)
    , read_pos_(0) {
}

IOBuffer::~IOBuffer() = default;

bool IOBuffer::write(const char* src, size_t len) {
    if (remaining() < len) {
        return false;
    }
    
    std::memcpy(data_.get() + size_, src, len);
    size_ += len;
    return true;
}

bool IOBuffer::read(char* dst, size_t len) {
    if (available() < len) {
        return false;
    }
    
    std::memcpy(dst, data_.get() + read_pos_, len);
    read_pos_ += len;
    return true;
}

void IOBuffer::consume(size_t len) {
    read_pos_ += len;
    if (read_pos_ >= size_) {
        clear();
    }
}

void IOBuffer::clear() {
    size_ = 0;
    read_pos_ = 0;
}

bool IOBuffer::reserve(size_t len) {
    return remaining() >= len;
}

} // namespace core
} // namespace best_server