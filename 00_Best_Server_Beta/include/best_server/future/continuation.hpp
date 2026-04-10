// Continuation - Chainable async operations
// 
// Provides utilities for chaining and composing async operations

#ifndef BEST_SERVER_FUTURE_CONTINUATION_HPP
#define BEST_SERVER_FUTURE_CONTINUATION_HPP

#include "future/future.hpp"
#include "future/promise.hpp"
#include <tuple>
#include <utility>

namespace best_server {
namespace future {

// Map operation
template<typename T, typename F>
auto map(Future<T>&& future, F&& func) -> Future<typename std::invoke_result<F(T)>::type> {
    return std::move(future).then(std::forward<F>(func));
}

// Flat map operation
template<typename T, typename F>
auto flat_map(Future<T>&& future, F&& func) -> typename std::invoke_result<F(T)>::type {
    using ResultFuture = typename std::invoke_result<F(T)>::type;
    using ResultType = typename ResultFuture::value_type;
    
    Promise<ResultType> promise;
    auto result = promise.get_future();
    
    std::move(future).then([func = std::forward<F>(func), promise = std::move(promise)](T value) mutable {
        try {
            auto inner_future = func(std::move(value));
            std::move(inner_future).then([promise = std::move(promise)](ResultType result) mutable {
                promise.set_value(std::move(result));
            });
        } catch (...) {
            promise.set_exception(std::current_exception());
        }
    });
    
    return result;
}

// Recover from errors
template<typename T, typename F>
Future<T> recover(Future<T>&& future, F&& func) {
    Promise<T> promise;
    auto result = promise.get_future();
    
    std::move(future).then([func = std::forward<F>(func), promise = std::move(promise)](auto&& value) mutable {
        try {
            promise.set_value(std::forward<decltype(value)>(value));
        } catch (...) {
            try {
                promise.set_value(func(std::current_exception()));
            } catch (...) {
                promise.set_exception(std::current_exception());
            }
        }
    });
    
    return result;
}

// Transform error
template<typename T, typename F>
Future<T> transform_error(Future<T>&& future, F&& func) {
    Promise<T> promise;
    auto result = promise.get_future();
    
    std::move(future).then([func = std::forward<F>(func), promise = std::move(promise)](auto&& value) mutable {
        try {
            promise.set_value(std::forward<decltype(value)>(value));
        } catch (const std::exception& e) {
            try {
                promise.set_exception(func(e));
            } catch (...) {
                promise.set_exception(std::current_exception());
            }
        }
    });
    
    return result;
}

// Execute callback regardless of success/failure
template<typename T, typename F>
Future<T> finally(Future<T>&& future, F&& func) {
    Promise<T> promise;
    auto result = promise.get_future();
    
    std::move(future).then([func = std::forward<F>(func), promise = std::move(promise)](auto&& value) mutable {
        try {
            func();
            promise.set_value(std::forward<decltype(value)>(value));
        } catch (...) {
            try {
                func();
            } catch (...) {
                // Ignore exception from finally
            }
            promise.set_exception(std::current_exception());
        }
    });
    
    return result;
}

// When all - wait for all futures to complete
template<typename... Futures>
auto when_all(Futures&&... futures) {
    using TupleType = std::tuple<typename std::decay<Futures>::type...>;
    
    struct AllState {
        std::atomic<size_t> completed{0};
        std::atomic<bool> has_error{false};
        std::exception_ptr exception;
        TupleType results;
        Promise<TupleType> promise;
    };
    
    auto state = std::make_shared<AllState>();
    state->completed = sizeof...(Futures);
    
    size_t index = 0;
    auto setup_future = [&](auto&& future) {
        std::move(future).then([state, index](auto&& value) {
            std::get<index>(state->results) = std::forward<decltype(value)>(value);
            if (state->completed.fetch_sub(1) == 1) {
                if (state->has_error) {
                    state->promise.set_exception(state->exception);
                } else {
                    state->promise.set_value(std::move(state->results));
                }
            }
        });
        ++index;
    };
    
    (setup_future(std::forward<Futures>(futures)), ...);
    
    return state->promise.get_future();
}

// When any - wait for first future to complete
template<typename... Futures>
auto when_any(Futures&&... futures) {
    struct AnyState {
        std::atomic<bool> completed{false};
        Promise<size_t> promise;
    };
    
    auto state = std::make_shared<AnyState>();
    
    size_t index = 0;
    auto setup_future = [&](auto&& future) {
        std::move(future).then([state, index](auto&&) {
            if (!state->completed.exchange(true)) {
                state->promise.set_value(index);
            }
        });
        ++index;
    };
    
    (setup_future(std::forward<Futures>(futures)), ...);
    
    return state->promise.get_future();
}

// Retry operation with backoff
template<typename T, typename F>
Future<T> retry(F&& func, size_t max_attempts, std::chrono::milliseconds initial_delay) {
    return retry_with_backoff(std::forward<F>(func), max_attempts, initial_delay, 2.0);
}

template<typename T, typename F>
Future<T> retry_with_backoff(F&& func, size_t max_attempts, 
                             std::chrono::milliseconds initial_delay, 
                             double backoff_factor) {
    // TODO: Implement retry_with_backoff with proper timer support
    // For now, just execute once
    [[maybe_unused]] auto _1 = max_attempts;
    [[maybe_unused]] auto _2 = initial_delay;
    [[maybe_unused]] auto _3 = backoff_factor;
    auto future = func();
    return future;
}

// Timeout
template<typename T>
Future<T> with_timeout(Future<T>&& future, std::chrono::milliseconds timeout) {
    [[maybe_unused]] auto _ = timeout;
    Promise<T> promise;
    auto result = promise.get_future();
    
    auto state = std::make_shared<std::atomic<bool>>(false);
    
    std::move(future).then([state, promise = std::move(promise)](auto&& value) mutable {
        if (!state->exchange(true)) {
            promise.set_value(std::forward<decltype(value)>(value));
        }
    });
    
    // Set up timeout timer (would need timer support)
    // For now, this is a placeholder
    
    return result;
}

} // namespace future
} // namespace best_server

#endif // BEST_SERVER_FUTURE_CONTINUATION_HPP