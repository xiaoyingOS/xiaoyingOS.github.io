// Coroutine Optimization - High-performance coroutine utilities
//
// Provides optimized coroutine utilities including:
// - Coroutine pool
// - Custom coroutine allocator
// - Coroutine stack optimization
// - Coroutine scheduling hints

#ifndef BEST_SERVER_FUTURE_COROUTINE_POOL_HPP
#define BEST_SERVER_FUTURE_COROUTINE_POOL_HPP

#include <coroutine>
#include <memory>
#include <vector>
#include <atomic>
#include <cstddef>

namespace best_server {
namespace future {

// Coroutine pool statistics
struct CoroutinePoolStats {
    size_t total_coroutines;
    size_t active_coroutines;
    size_t idle_coroutines;
    size_t total_memory_used;
    size_t peak_memory_used;
    size_t allocations;
    size_t deallocations;
};

// Coroutine frame info
struct CoroutineFrame {
    void* handle;
    size_t size;
    size_t alignment;
};

// Coroutine allocator for custom memory management
class CoroutineAllocator {
public:
    // Allocate coroutine frame
    static void* allocate(size_t size);
    
    // Deallocate coroutine frame
    static void deallocate(void* ptr, size_t size);
    
    // Get pool statistics
    static CoroutinePoolStats get_stats();
    
    // Reset statistics
    static void reset_stats();
    
    // Set maximum pool size
    static void set_max_pool_size(size_t max_size);
    
    // Get maximum pool size
    static size_t get_max_pool_size();
    
    // Enable/disable pooling
    static void enable_pooling(bool enable);
    
    // Check if pooling is enabled
    static bool is_pooling_enabled();
    
private:
    static constexpr size_t DEFAULT_MAX_POOL_SIZE = 1024;
    static constexpr size_t FRAME_ALIGNMENT = 16;
    
    struct PoolEntry {
        void* frame;
        size_t size;
        PoolEntry* next;
    };
    
    static PoolEntry* free_list_;
    static std::atomic<size_t> pool_size_;
    static std::atomic<size_t> max_pool_size_;
    static std::atomic<bool> pooling_enabled_;
    
    // Statistics
    static std::atomic<size_t> total_allocations_;
    static std::atomic<size_t> total_deallocations_;
    static std::atomic<size_t> total_memory_used_;
    static std::atomic<size_t> peak_memory_used_;
    static std::atomic<size_t> active_coroutines_;
};

// Coroutine handle with custom allocator
template<typename Promise>
class CoroutineHandle {
public:
    using promise_type = Promise;
    using handle_type = std::coroutine_handle<Promise>;
    
    CoroutineHandle() = default;
    explicit CoroutineHandle(handle_type h) : handle_(h) {}
    
    ~CoroutineHandle() {
        if (handle_) {
            handle_.destroy();
        }
    }
    
    // Move constructor
    CoroutineHandle(CoroutineHandle&& other) noexcept : handle_(other.handle_) {
        other.handle_ = nullptr;
    }
    
    // Move assignment
    CoroutineHandle& operator=(CoroutineHandle&& other) noexcept {
        if (this != &other) {
            if (handle_) {
                handle_.destroy();
            }
            handle_ = other.handle_;
            other.handle_ = nullptr;
        }
        return *this;
    }
    
    // Disable copy
    CoroutineHandle(const CoroutineHandle&) = delete;
    CoroutineHandle& operator=(const CoroutineHandle&) = delete;
    
    // Check if valid
    explicit operator bool() const noexcept { return handle_ != nullptr; }
    
    // Resume coroutine
    void resume() {
        if (handle_) {
            handle_.resume();
        }
    }
    
    // Check if coroutine is done
    bool done() const noexcept {
        return handle_ ? handle_.done() : true;
    }
    
    // Get promise
    Promise& promise() {
        return handle_.promise();
    }
    
    // Get underlying handle
    handle_type get() const { return handle_; }
    
private:
    handle_type handle_;
};

// Coroutine task with custom allocation
template<typename T>
class CoroutineTask {
public:
    struct promise_type {
        T value_;
        CoroutineTask get_return_object() {
            return CoroutineTask{std::coroutine_handle<promise_type>::from_promise(*this)};
        }
        std::suspend_never initial_suspend() { return {}; }
        std::suspend_never final_suspend() noexcept { return {}; }
        void return_value(T value) { value_ = std::move(value); }
        void unhandled_exception() { /* Handle exception */ }
        
        // Custom allocator
        static void* operator new(size_t size) {
            return CoroutineAllocator::allocate(size);
        }
        
        static void operator delete(void* ptr, size_t size) {
            CoroutineAllocator::deallocate(ptr, size);
        }
    };
    
    using handle_type = std::coroutine_handle<promise_type>;
    
    explicit CoroutineTask(handle_type h) : handle_(h) {}
    
    ~CoroutineTask() {
        if (handle_) {
            handle_.destroy();
        }
    }
    
    // Move constructor
    CoroutineTask(CoroutineTask&& other) noexcept : handle_(other.handle_) {
        other.handle_ = nullptr;
    }
    
    // Move assignment
    CoroutineTask& operator=(CoroutineTask&& other) noexcept {
        if (this != &other) {
            if (handle_) {
                handle_.destroy();
            }
            handle_ = other.handle_;
            other.handle_ = nullptr;
        }
        return *this;
    }
    
    // Disable copy
    CoroutineTask(const CoroutineTask&) = delete;
    CoroutineTask& operator=(const CoroutineTask&) = delete;
    
    // Get result
    T get() {
        while (!handle_.done()) {
            handle_.resume();
        }
        return std::move(handle_.promise().value_);
    }
    
    // Check if done
    bool done() const noexcept {
        return handle_.done();
    }
    
private:
    handle_type handle_;
};

// Specialization for void
template<>
class CoroutineTask<void> {
public:
    struct promise_type {
        CoroutineTask get_return_object() {
            return CoroutineTask{std::coroutine_handle<promise_type>::from_promise(*this)};
        }
        std::suspend_never initial_suspend() { return {}; }
        std::suspend_never final_suspend() noexcept { return {}; }
        void return_void() {}
        void unhandled_exception() { /* Handle exception */ }
        
        // Custom allocator
        static void* operator new(size_t size) {
            return CoroutineAllocator::allocate(size);
        }
        
        static void operator delete(void* ptr, size_t size) {
            CoroutineAllocator::deallocate(ptr, size);
        }
    };
    
    using handle_type = std::coroutine_handle<promise_type>;
    
    explicit CoroutineTask(handle_type h) : handle_(h) {}
    
    ~CoroutineTask() {
        if (handle_) {
            handle_.destroy();
        }
    }
    
    // Move constructor
    CoroutineTask(CoroutineTask&& other) noexcept : handle_(other.handle_) {
        other.handle_ = nullptr;
    }
    
    // Move assignment
    CoroutineTask& operator=(CoroutineTask&& other) noexcept {
        if (this != &other) {
            if (handle_) {
                handle_.destroy();
            }
            handle_ = other.handle_;
            other.handle_ = nullptr;
        }
        return *this;
    }
    
    // Disable copy
    CoroutineTask(const CoroutineTask&) = delete;
    CoroutineTask& operator=(const CoroutineTask&) = delete;
    
    // Get result
    void get() {
        while (!handle_.done()) {
            handle_.resume();
        }
    }
    
    // Check if done
    bool done() const noexcept {
        return handle_.done();
    }
    
private:
    handle_type handle_;
};

// Coroutine scheduling hints
enum class CoroutineHint {
    None,           // No hint
    CPUIntensive,   // CPU intensive
    IOIntensive,    // I/O intensive
    ShortRunning,   // Short running
    LongRunning,    // Long running
    HighPriority,   // High priority
    LowPriority     // Low priority
};

// Coroutine with scheduling hints
template<typename T>
class CoroutineTaskWithHints {
public:
    struct promise_type {
        T value_;
        CoroutineHint hint_ = CoroutineHint::None;
        
        CoroutineTaskWithHints get_return_object() {
            return CoroutineTaskWithHints{
                std::coroutine_handle<promise_type>::from_promise(*this),
                hint_
            };
        }
        
        std::suspend_never initial_suspend() { return {}; }
        std::suspend_never final_suspend() noexcept { return {}; }
        void return_value(T value) { value_ = std::move(value); }
        void unhandled_exception() { /* Handle exception */ }
        
        void set_hint(CoroutineHint hint) { hint_ = hint; }
        
        // Custom allocator
        static void* operator new(size_t size) {
            return CoroutineAllocator::allocate(size);
        }
        
        static void operator delete(void* ptr, size_t size) {
            CoroutineAllocator::deallocate(ptr, size);
        }
    };
    
    using handle_type = std::coroutine_handle<promise_type>;
    
    explicit CoroutineTaskWithHints(handle_type h, CoroutineHint hint)
        : handle_(h), hint_(hint) {}
    
    ~CoroutineTaskWithHints() {
        if (handle_) {
            handle_.destroy();
        }
    }
    
    // Move constructor
    CoroutineTaskWithHints(CoroutineTaskWithHints&& other) noexcept
        : handle_(other.handle_), hint_(other.hint_) {
        other.handle_ = nullptr;
    }
    
    // Move assignment
    CoroutineTaskWithHints& operator=(CoroutineTaskWithHints&& other) noexcept {
        if (this != &other) {
            if (handle_) {
                handle_.destroy();
            }
            handle_ = other.handle_;
            hint_ = other.hint_;
            other.handle_ = nullptr;
        }
        return *this;
    }
    
    // Disable copy
    CoroutineTaskWithHints(const CoroutineTaskWithHints&) = delete;
    CoroutineTaskWithHints& operator=(const CoroutineTaskWithHints&) = delete;
    
    // Get result
    T get() {
        while (!handle_.done()) {
            handle_.resume();
        }
        return std::move(handle_.promise().value_);
    }
    
    // Check if done
    bool done() const noexcept {
        return handle_.done();
    }
    
    // Get hint
    CoroutineHint hint() const noexcept { return hint_; }
    
private:
    handle_type handle_;
    CoroutineHint hint_;
};

// Coroutine stack size optimization
struct CoroutineStackSize {
    static constexpr size_t Default = 64 * 1024;     // 64KB
    static constexpr size_t Small = 32 * 1024;        // 32KB
    static constexpr size_t Large = 256 * 1024;       // 256KB
    static constexpr size_t Huge = 1024 * 1024;       // 1MB
};

// Helper functions

// Set coroutine stack size (platform-dependent)
bool set_coroutine_stack_size(size_t size);

// Get coroutine stack size
size_t get_coroutine_stack_size();

// Optimize coroutine allocation
void optimize_coroutine_allocation(bool enable);

// Check if coroutine optimization is enabled
bool is_coroutine_optimization_enabled();

} // namespace future
} // namespace best_server

#endif // BEST_SERVER_FUTURE_COROUTINE_POOL_HPP