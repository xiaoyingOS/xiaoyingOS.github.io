// Coroutine Support - C++20 coroutines for async/await syntax
//
// Provides coroutine support for writing async code in sync style:
// - co_await support for futures
// - Coroutine task wrapper
// - Async/await syntax sugar
// - Coroutine scheduler integration

#ifndef BEST_SERVER_COROUTINE_COROUTINE_HPP
#define BEST_SERVER_COROUTINE_COROUTINE_HPP

#include "best_server/future/future.hpp"
#include <coroutine>
#include <exception>
#include <variant>
#include <memory>

namespace best_server {
namespace coroutine {

// Coroutine task type
template<typename T>
class Task {
public:
    struct promise_type {
        Task get_return_object() {
            return Task{std::coroutine_handle<promise_type>::from_promise(*this)};
        }
        
        std::suspend_never initial_suspend() { return {}; }
        std::suspend_never final_suspend() noexcept { return {}; }
        
        void return_value(T value) {
            result_ = std::move(value);
        }
        
        void unhandled_exception() {
            exception_ = std::current_exception();
        }
        
        T result_;
        std::exception_ptr exception_;
    };
    
    Task(std::coroutine_handle<promise_type> handle) : handle_(handle) {}
    
    Task(Task&& other) noexcept : handle_(other.handle_) {
        other.handle_ = nullptr;
    }
    
    ~Task() {
        if (handle_) {
            handle_.destroy();
        }
    }
    
    // Awaiter for co_await
    class Awaiter {
    public:
        Awaiter(std::coroutine_handle<promise_type> handle) : handle_(handle) {}
        
        bool await_ready() {
            return handle_.done();
        }
        
        void await_suspend(std::coroutine_handle<> awaiting_handle) {
            // Store awaiting coroutine for resumption
            awaiting_coro_ = awaiting_handle;
        }
        
        T await_resume() {
            if (handle_.promise().exception_) {
                std::rethrow_exception(handle_.promise().exception_);
            }
            return std::move(handle_.promise().result_);
        }
        
    private:
        std::coroutine_handle<promise_type> handle_;
        std::coroutine_handle<> awaiting_coro_;
    };
    
    Awaiter operator co_await() {
        return Awaiter{handle_};
    }
    
    bool done() const {
        return handle_.done();
    }
    
private:
    std::coroutine_handle<promise_type> handle_;
};

// Specialization for void
template<>
class Task<void> {
public:
    struct promise_type {
        Task get_return_object() {
            return Task{std::coroutine_handle<promise_type>::from_promise(*this)};
        }
        
        std::suspend_never initial_suspend() { return {}; }
        std::suspend_never final_suspend() noexcept { return {}; }
        
        void return_void() {}
        
        void unhandled_exception() {
            exception_ = std::current_exception();
        }
        
        std::exception_ptr exception_;
    };
    
    Task(std::coroutine_handle<promise_type> handle) : handle_(handle) {}
    
    Task(Task&& other) noexcept : handle_(other.handle_) {
        other.handle_ = nullptr;
    }
    
    ~Task() {
        if (handle_) {
            handle_.destroy();
        }
    }
    
    class Awaiter {
    public:
        Awaiter(std::coroutine_handle<promise_type> handle) : handle_(handle) {}
        
        bool await_ready() {
            return handle_.done();
        }
        
        void await_suspend(std::coroutine_handle<> awaiting_handle) {
            awaiting_coro_ = awaiting_handle;
        }
        
        void await_resume() {
            if (handle_.promise().exception_) {
                std::rethrow_exception(handle_.promise().exception_);
            }
        }
        
    private:
        std::coroutine_handle<promise_type> handle_;
        std::coroutine_handle<> awaiting_coro_;
    };
    
    Awaiter operator co_await() {
        return Awaiter{handle_};
    }
    
    bool done() const {
        return handle_.done();
    }
    
private:
    std::coroutine_handle<promise_type> handle_;
};

// Future awaiter for co_await
template<typename T>
class FutureAwaiter {
public:
    FutureAwaiter(future::Future<T> future) : future_(std::move(future)) {}
    
    bool await_ready() {
        return future_.is_ready();
    }
    
    void await_suspend(std::coroutine_handle<> awaiting_handle) {
        // Register callback to resume coroutine when future is ready
        future_.then([awaiting_handle](const T&) {
            if (!awaiting_handle.done()) {
                awaiting_handle.resume();
            }
        });
    }
    
    T await_resume() {
        return future_.get();
    }
    
private:
    future::Future<T> future_;
};

// Enable co_await for Future<T>
template<typename T>
FutureAwaiter<T> operator co_await(future::Future<T>&& future) {
    return FutureAwaiter<T>{std::move(future)};
}

template<typename T>
FutureAwaiter<T> operator co_await(future::Future<T>& future) {
    return FutureAwaiter<T>{future};
}

// Async function wrapper
template<typename F, typename... Args>
auto async(F&& f, Args&&... args) -> Task<decltype(f(std::forward<Args>(args)...))> {
    co_return f(std::forward<Args>(args)...);
}

// Sleep coroutine
inline Task<void> sleep_for(std::chrono::milliseconds duration) {
    future::Promise<void> promise;
    auto future = promise.get_future();
    
    // Set timeout (simplified)
    std::thread([duration, p = std::move(promise)]() mutable {
        std::this_thread::sleep_for(duration);
        p.set_value();
    }).detach();
    
    co_await future;
}

// Sleep coroutine using scheduler
inline Task<void> sleep_for_async(std::chrono::milliseconds duration) {
    co_await sleep_for(duration);
}

} // namespace coroutine
} // namespace best_server

#endif // BEST_SERVER_COROUTINE_COROUTINE_HPP