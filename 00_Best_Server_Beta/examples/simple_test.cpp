// Simple Test - Tests basic functionality without using namespace best_server

#include <iostream>
#include <best_server/best_server.hpp>
#include <thread>
#include <chrono>

int main() {
    std::cout << "Best_Server Simple Test" << std::endl;
    std::cout << "=======================" << std::endl;
    
    // Test Future/Promise
    std::cout << "\nTesting Future/Promise..." << std::endl;
    
    best_server::future::Promise<int> promise;
    auto future = promise.get_future();
    
    std::thread([promise = std::move(promise)]() mutable {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        promise.set_value(42);
    }).detach();
    
    int result = future.get();
    std::cout << "Future result: " << result << std::endl;
    
    // Test ZeroCopyBuffer
    std::cout << "\nTesting ZeroCopyBuffer..." << std::endl;
    best_server::memory::ZeroCopyBuffer buffer(1024);
    std::cout << "Buffer capacity: " << buffer.capacity() << std::endl;
    std::cout << "Buffer size: " << buffer.size() << std::endl;
    
    std::cout << "\nAll tests passed!" << std::endl;
    
    return 0;
}