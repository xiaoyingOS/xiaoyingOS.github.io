// IOEventLoop - Cross-platform I/O event loop implementation

// Define platform macro before including headers
#ifndef BEST_SERVER_PLATFORM_LINUX
#define BEST_SERVER_PLATFORM_LINUX 1
#endif

#include "best_server/io/io_event_loop.hpp"
#include <unistd.h>
#include <fcntl.h>
#include <cstring>
#include <sys/epoll.h>

namespace best_server {
namespace io {

// IOEventLoop implementation
IOEventLoop::IOEventLoop() {
#if BEST_SERVER_PLATFORM_LINUX
    epoll_fd_ = epoll_create1(EPOLL_CLOEXEC);
    epoll_events_ = new epoll_event[MAX_EVENTS];
    
    pipe(wake_pipe_);
    fcntl(wake_pipe_[0], F_SETFL, O_NONBLOCK);
    fcntl(wake_pipe_[1], F_SETFL, O_NONBLOCK);
    
    struct epoll_event ev;
    ev.events = EPOLLIN;
    ev.data.fd = wake_pipe_[0];
    epoll_ctl(epoll_fd_, EPOLL_CTL_ADD, wake_pipe_[0], &ev);
    
#elif BEST_SERVER_PLATFORM_MACOS
    kqueue_fd_ = kqueue();
    kqueue_events_ = new kevent[MAX_EVENTS];
    
    pipe(wake_pipe_);
    fcntl(wake_pipe_[0], F_SETFL, O_NONBLOCK);
    fcntl(wake_pipe_[1], F_SETFL, O_NONBLOCK);
    
    struct kevent ev;
    EV_SET(&ev, wake_pipe_[0], EVFILT_READ, EV_ADD | EV_CLEAR, 0, 0, nullptr);
    kevent(kqueue_fd_, &ev, 1, nullptr, 0, nullptr);
    
#elif BEST_SERVER_PLATFORM_WINDOWS
    iocp_port_ = CreateIoCompletionPort(INVALID_HANDLE_VALUE, nullptr, 0, 0);
    iocp_events_ = new OVERLAPPED_ENTRY[MAX_EVENTS];
    
    wake_pipe_[0] = CreateEvent(nullptr, FALSE, FALSE, nullptr);
    wake_pipe_[1] = wake_pipe_[0];
#endif
}

IOEventLoop::~IOEventLoop() {
    stop();
    
#if BEST_SERVER_PLATFORM_LINUX
    if (epoll_fd_ != -1) {
        close(epoll_fd_);
    }
    delete[] epoll_events_;
    close(wake_pipe_[0]);
    close(wake_pipe_[1]);
    
#elif BEST_SERVER_PLATFORM_MACOS
    if (kqueue_fd_ != -1) {
        close(kqueue_fd_);
    }
    delete[] kqueue_events_;
    close(wake_pipe_[0]);
    close(wake_pipe_[1]);
    
#elif BEST_SERVER_PLATFORM_WINDOWS
    if (iocp_port_ != INVALID_HANDLE_VALUE) {
        CloseHandle(iocp_port_);
    }
    delete[] iocp_events_;
    CloseHandle(wake_pipe_[0]);
#endif
}

void IOEventLoop::run() {
    
    // Register this event loop for the current thread
    auto thread_id = std::this_thread::get_id();
    {
        std::lock_guard<std::mutex> lock(g_event_loops_mutex_);
        g_event_loops_[thread_id] = this;
    }
    
    running_.store(true);
    
    int loop_count = 0;
    while (running_.load()) {
        loop_count++;
        if (loop_count % 10 == 0) {
        }
        poll_events(100);
        run_pending_tasks();
    }
    
    // Unregister this event loop
    {
        std::lock_guard<std::mutex> lock(g_event_loops_mutex_);
        g_event_loops_.erase(thread_id);
    }
    
}

void IOEventLoop::stop() {
    if (!running_.load()) {
        return;
    }
    
    running_.store(false);
    wake_up();
}

bool IOEventLoop::register_fd(int fd, EventType events, EventCallback callback) {
    printf("DEBUG: register_fd called, fd=%d, events=%d\n", fd, static_cast<int>(events));
    // fflush(stdout);
    
    FDRegistration reg;
    reg.fd = fd;
    reg.callback = std::move(callback);
    reg.events = events;
    reg.user_data = nullptr;
    
#if !BEST_SERVER_PLATFORM_WINDOWS
    int flags = fcntl(fd, F_GETFL, 0);
    fcntl(fd, F_SETFL, flags | O_NONBLOCK);
#endif
    
    bool already_registered = (fd_map_.find(fd) != fd_map_.end());
    
#if BEST_SERVER_PLATFORM_LINUX
    struct epoll_event ev;
    // Use edge-triggered mode to avoid infinite loops
    ev.events = EPOLLET;  // Edge-triggered mode
    
    if (static_cast<uint32_t>(events) & static_cast<uint32_t>(EventType::Read)) ev.events |= EPOLLIN;
    if (static_cast<uint32_t>(events) & static_cast<uint32_t>(EventType::Write)) ev.events |= EPOLLOUT;
    if (static_cast<uint32_t>(events) & static_cast<uint32_t>(EventType::Accept)) ev.events |= EPOLLIN;
    // Use EPOLLONESHOT for all events
    if (static_cast<uint32_t>(events) & static_cast<uint32_t>(EventType::OneShot)) {
        ev.events |= EPOLLONESHOT;
    }
    ev.data.fd = fd;
    
    // Check if fd is already registered
    int op = EPOLL_CTL_ADD;
    if (already_registered) {
        op = EPOLL_CTL_MOD;
        printf("DEBUG: fd=%d already registered, using EPOLL_CTL_MOD\n", fd);
        // fflush(stdout);
    }
    
    if (epoll_ctl(epoll_fd_, op, fd, &ev) == -1) {
        printf("DEBUG: epoll_ctl failed for fd=%d, op=%d, errno=%d\n", fd, op, errno);
        // fflush(stdout);
        return false;
    }
    
    printf("DEBUG: epoll_ctl success for fd=%d, op=%d\n", fd, op);
    // fflush(stdout);
    
#elif BEST_SERVER_PLATFORM_MACOS
    struct kevent ev[2];
    int nev = 0;
    
    if (events & EventType::Read) {
        EV_SET(&ev[nev++], fd, EVFILT_READ, EV_ADD | EV_CLEAR, 0, 0, nullptr);
    }
    if (events & EventType::Write) {
        EV_SET(&ev[nev++], fd, EVFILT_WRITE, EV_ADD | EV_CLEAR, 0, 0, nullptr);
    }
    
    if (kevent(kqueue_fd_, ev, nev, nullptr, 0, nullptr) == -1) {
        return false;
    }
    
#elif BEST_SERVER_PLATFORM_WINDOWS
    HANDLE handle = reinterpret_cast<HANDLE>(fd);
    CreateIoCompletionPort(handle, iocp_port_, fd, 0);
#endif
    
    // Always update fd_map_[fd] with new callback and events
    // This ensures that even if fd was already registered, the callback is updated
    fd_map_[fd] = std::move(reg);
    if (!already_registered) {
        ++stats_.registered_fds;
    }
    
    return true;
}

bool IOEventLoop::modify_fd(int fd, EventType events) {
    printf("DEBUG: modify_fd called, fd=%d, events=%d\n", fd, static_cast<int>(events));
    // fflush(stdout);
    
    auto it = fd_map_.find(fd);
    if (it == fd_map_.end()) {
        printf("DEBUG: modify_fd failed, fd=%d not found in fd_map_\n", fd);
        // fflush(stdout);
        return false;
    }
    
    it->second.events = events;
    
#if BEST_SERVER_PLATFORM_LINUX
    struct epoll_event ev;
    ev.events = EPOLLET;  // Edge-triggered mode
    if (static_cast<uint32_t>(events) & static_cast<uint32_t>(EventType::Read)) ev.events |= EPOLLIN;
    if (static_cast<uint32_t>(events) & static_cast<uint32_t>(EventType::Write)) ev.events |= EPOLLOUT;
    if (static_cast<uint32_t>(events) & static_cast<uint32_t>(EventType::OneShot)) ev.events |= EPOLLONESHOT;
    ev.data.fd = fd;
    
    int result = epoll_ctl(epoll_fd_, EPOLL_CTL_MOD, fd, &ev);
    printf("DEBUG: epoll_ctl EPOLL_CTL_MOD for fd=%d, result=%d, errno=%d\n", fd, result, errno);
    // fflush(stdout);
    
    return result != -1;
    
#elif BEST_SERVER_PLATFORM_MACOS
    // Remove and re-add
    struct kevent ev[2];
    int nev = 0;
    EV_SET(&ev[nev++], fd, EVFILT_READ, EV_DELETE, 0, 0, nullptr);
    EV_SET(&ev[nev++], fd, EVFILT_WRITE, EV_DELETE, 0, 0, nullptr);
    kevent(kqueue_fd_, ev, nev, nullptr, 0, nullptr);
    
    nev = 0;
    if (events & EventType::Read) {
        EV_SET(&ev[nev++], fd, EVFILT_READ, EV_ADD | EV_CLEAR, 0, 0, nullptr);
    }
    if (events & EventType::Write) {
        EV_SET(&ev[nev++], fd, EVFILT_WRITE, EV_ADD | EV_CLEAR, 0, 0, nullptr);
    }
    
    return kevent(kqueue_fd_, ev, nev, nullptr, 0, nullptr) != -1;
    
#elif BEST_SERVER_PLATFORM_WINDOWS
    return true;
#else
    return false;
#endif
}

bool IOEventLoop::unregister_fd(int fd) {
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

void IOEventLoop::wake_up() {
#if BEST_SERVER_PLATFORM_LINUX || BEST_SERVER_PLATFORM_MACOS
    char c = 1;
    write(wake_pipe_[1], &c, 1);
#elif BEST_SERVER_PLATFORM_WINDOWS
    SetEvent(wake_pipe_[0]);
#endif
    ++stats_.wake_count;
}

bool IOEventLoop::run_once(int timeout_ms) {
    poll_events(timeout_ms);
    return running_.load();
}

bool IOEventLoop::poll_events(int timeout_ms) {
    static int poll_count = 0;
    poll_count++;
    
#ifdef BEST_SERVER_PLATFORM_LINUX
    if (poll_count <= 20) {
    }
#else
    if (poll_count <= 20) {
    }
#endif
    
    (void)timeout_ms;  // Suppress unused parameter warning - used in platform-specific code
    ++stats_.poll_count;
    
#if BEST_SERVER_PLATFORM_LINUX
    int nfds = epoll_wait(epoll_fd_, epoll_events_, MAX_EVENTS, timeout_ms);
    
    if (nfds == -1) {
        if (errno == EINTR) return true;
        return false;
    }
    
    if (nfds == 0) {
        if (poll_count <= 20) {
        }
        return true;
    }
    
    ++stats_.events_processed;
    
    for (int i = 0; i < nfds; ++i) {
        int fd = epoll_events_[i].data.fd;
        
        if (fd == wake_pipe_[0]) {
            char buf[256];
            read(fd, buf, sizeof(buf));
            continue;
        }
        
        auto it = fd_map_.find(fd);
        if (it != fd_map_.end()) {
            EventType events = EventType::None;
            if (epoll_events_[i].events & EPOLLIN) events = static_cast<EventType>(static_cast<uint32_t>(events) | static_cast<uint32_t>(EventType::Read));
            if (epoll_events_[i].events & EPOLLOUT) events = static_cast<EventType>(static_cast<uint32_t>(events) | static_cast<uint32_t>(EventType::Write));
            if (epoll_events_[i].events & EPOLLERR) events = static_cast<EventType>(static_cast<uint32_t>(events) | static_cast<uint32_t>(EventType::Error));
            if (epoll_events_[i].events & EPOLLHUP) events = static_cast<EventType>(static_cast<uint32_t>(events) | static_cast<uint32_t>(EventType::Hangup));
            
            ++stats_.events_dispatched;
            it->second.callback(events);
        } else {
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
        
        if (fd == wake_pipe_[0]) {
            char buf[256];
            read(fd, buf, sizeof(buf));
            continue;
        }
        
        auto it = fd_map_.find(fd);
        if (it != fd_map_.end()) {
            EventType events = EventType::None;
            if (kqueue_events_[i].filter == EVFILT_READ) events |= EventType::Read;
            if (kqueue_events_[i].filter == EVFILT_WRITE) events |= EventType::Write;
            if (kqueue_events_[i].flags & EV_ERROR) events |= EventType::Error;
            
            ++stats_.events_dispatched;
            it->second.callback(events, fd);
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
        EventType events = success ? EventType::Read : EventType::Error;
        it->second.callback(events, fd);
    }
    
    return true;
#else
    return false;
#endif
}

void IOEventLoop::dispatch_events() {
    // Events are dispatched in poll_events
}

void IOEventLoop::schedule(std::function<void()> task) {
    std::lock_guard<std::mutex> lock(task_queue_mutex_);
    task_queue_.push_back(std::move(task));
    wake_up();
}

void IOEventLoop::run_pending_tasks() {
    std::vector<std::function<void()>> tasks;
    {
        std::lock_guard<std::mutex> lock(task_queue_mutex_);
        tasks = std::move(task_queue_);
        task_queue_.clear();
    }
    
    if (!tasks.empty()) {
    }
    
    for (auto& task : tasks) {
        try {
            task();
        } catch (...) {
        }
    }
}

IOEventLoop* IOEventLoop::current() {
    auto thread_id = std::this_thread::get_id();
    std::lock_guard<std::mutex> lock(g_event_loops_mutex_);
    auto it = g_event_loops_.find(thread_id);
    return (it != g_event_loops_.end()) ? it->second : nullptr;
}

std::unordered_map<std::thread::id, IOEventLoop*> IOEventLoop::g_event_loops_;
std::mutex IOEventLoop::g_event_loops_mutex_;

} // namespace io
} // namespace best_server