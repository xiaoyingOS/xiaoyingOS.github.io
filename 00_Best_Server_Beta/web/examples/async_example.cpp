// Async Programming Example
// 
// This example demonstrates how to use the async/future model
// in Best_Server.

#include <best_server/best_server.hpp>
#include <iostream>
#include <thread>
#include <chrono>

using namespace best_server::future;

// Example async function
best_server::future::Future<int> async_add(int a, int b) {
    best_server::future::Promise<int> promise;
    auto future = promise.get_future();
    
    // Simulate async operation
    std::thread([a, b, promise = std::move(promise)]() mutable {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        promise.set_value(a + b);
    }).detach();
    
    return future;
}

// Example async function with exception
best_server::future::Future<int> async_divide(int a, int b) {
    best_server::future::Promise<int> promise;
    auto future = promise.get_future();
    
    std::thread([a, b, promise = std::move(promise)]() mutable {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        if (b == 0) {
            promise.set_exception(std::make_exception_ptr(
                std::runtime_error("Division by zero")
            ));
        } else {
            promise.set_value(a / b);
        }
    }).detach();
    
    return future;
}

// Example: Chaining futures
void example_chaining() {
    std::cout << "\nExample: Chaining Futures" << std::endl;
    
    auto future = async_add(5, 3)
        .then([](int result) {
            std::cout << "5 + 3 = " << result << std::endl;
            return async_add(result, 2);
        })
        .then([](int result) {
            std::cout << "Result + 2 = " << result << std::endl;
            return result * 10;
        })
        .then([](int result) {
            std::cout << "Final result: " << result << std::endl;
        });
    
    future.wait();
}

// Example: Error handling
void example_error_handling() {
    std::cout << "\nExample: Error Handling" << std::endl;
    
    auto future = async_divide(10, 0)
        .then([](int result) {
            std::cout << "Result: " << result << std::endl;
            return result;
        })
        .recover([](std::exception_ptr e) {
            try {
                std::rethrow_exception(e);
            } catch (const std::exception& ex) {
                std::cout << "Caught exception: " << ex.what() << std::endl;
                return 0;
            }
        });
    
    future.wait();
}

// Example: When all
void example_when_all() {
    std::cout << "\nExample: When All" << std::endl;
    
    auto f1 = async_add(1, 2);
    auto f2 = async_add(3, 4);
    auto f3 = async_add(5, 6);
    
    auto all_future = when_all(std::move(f1), std::move(f2), std::move(f3))
        .then([](auto&& results) {
            auto [r1, r2, r3] = results;
            std::cout << "Results: " << r1.get() << ", " << r2.get() << ", " << r3.get() << std::endl;
        });
    
    all_future.wait();
}

// Example: Retry with backoff
best_server::future::Future<int> async_operation_with_retry(int attempt) {
    best_server::future::Promise<int> promise;
    auto future = promise.get_future();
    
    std::thread([attempt, promise = std::move(promise)]() mutable {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        if (attempt < 3) {
            promise.set_exception(std::make_exception_ptr(
                std::runtime_error("Operation failed")
            ));
        } else {
            promise.set_value(42);
        }
    }).detach();
    
    return future;
}

void example_retry() {
    std::cout << "\nExample: Retry with Backoff" << std::endl;
    
    int attempt = 0;
    auto future = retry<int>(
        [&attempt]() {
            return async_operation_with_retry(attempt++);
        },
        5,
        std::chrono::milliseconds(100)
    ).then([](int result) {
        std::cout << "Operation succeeded after retries: " << result << std::endl;
    });
    
    future.wait();
}

#if BEST_SERVER_HAS_COROUTINES
// Example: Coroutines
best_server::future::Future<int> coroutine_add(int a, int b) {
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    co_return a + b;
}

best_server::future::Future<int> coroutine_calculate() {
    int result1 = co_await coroutine_add(5, 3);
    int result2 = co_await coroutine_add(result1, 2);
    co_return result2 * 10;
}

void example_coroutines() {
    std::cout << "\nExample: Coroutines" << std::endl;
    
    auto future = coroutine_calculate()
        .then([](int result) {
            std::cout << "Coroutine result: " << result << std::endl;
        });
    
    future.wait();
}
#endif

int main() {
    std::cout << "Best_Server - Async Programming Example" << std::endl;
    std::cout << "=========================================" << std::endl;
    
    example_chaining();
    example_error_handling();
    example_when_all();
    example_retry();
    
#if BEST_SERVER_HAS_COROUTINES
    example_coroutines();
#endif
    
    std::cout << "\nAll examples completed!" << std::endl;
    
    return 0;
}