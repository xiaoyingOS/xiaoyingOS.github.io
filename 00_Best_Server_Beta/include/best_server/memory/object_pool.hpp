// ObjectPool - High-performance object pool with per-core sharding
// 
// Implements an optimized object pool with:
// - Per-core sharding for cache locality
// - Lock-free operations
// - Minimal memory overhead
// - Automatic cleanup

#ifndef BEST_SERVER_MEMORY_OBJECT_POOL_HPP
#define BEST_SERVER_MEMORY_OBJECT_POOL_HPP

#include <memory>
#include <vector>
#include <atomic>
#include <cstdint>
#include <functional>
#include <array>

namespace best_server {
namespace memory {

// Object pool statistics
struct ObjectPoolStats {
    uint64_t total_allocations{0};
    uint64_t total_releases{0};
    uint64_t cache_hits{0};
    uint64_t cache_misses{0};
    uint32_t pool_size{0};
    uint32_t active_objects{0};
};

// Thread-local object pool shard
template<typename T>
class PoolShard {
public:
    static constexpr size_t DEFAULT_CAPACITY = 1024;
    
    PoolShard(size_t capacity = DEFAULT_CAPACITY)
        : capacity_(capacity)
        , size_(0)
        , objects_(capacity) {
        for (size_t i = 0; i < capacity; ++i) {
            objects_[i] = nullptr;
        }
    }
    
    ~PoolShard() {
        for (size_t i = 0; i < capacity_; ++i) {
            if (objects_[i]) {
                delete objects_[i];
            }
        }
    }
    
    // Acquire an object
    T* acquire() {
        if (size_ > 0) {
            --size_;
            T* obj = objects_[size_];
            objects_[size_] = nullptr;
            return obj;
        }
        return nullptr;
    }
    
    // Release an object
    bool release(T* obj) {
        if (size_ < capacity_) {
            objects_[size_++] = obj;
            return true;
        }
        return false;
    }
    
    // Get current size
    size_t size() const { return size_; }
    
    // Get capacity
    size_t capacity() const { return capacity_; }
    
private:
    size_t capacity_;
    std::atomic<size_t> size_;
    std::vector<T*> objects_;
};

// Global object pool with per-core sharding
template<typename T>
class ObjectPool {
public:
    static constexpr size_t MAX_SHARDS = 128;
    
    ObjectPool(size_t shard_capacity = PoolShard<T>::DEFAULT_CAPACITY);
    ~ObjectPool();
    
    // Acquire an object from the pool
    T* acquire();
    
    // Release an object to the pool
    void release(T* obj);
    
    // Get statistics
    ObjectPoolStats stats() const;
    
    // Set factory function for creating new objects
    void set_factory(std::function<T*()> factory);
    
    // Set destructor function for cleaning up objects
    void set_destructor(std::function<void(T*)> destructor);
    
    // Resize the pool
    void resize(size_t shard_capacity);
    
    // Clear all objects
    void clear();
    
private:
    // Get shard ID for current thread
    int get_shard_id() const;
    
    // Create a new object
    T* create_object();
    
    // Destroy an object
    void destroy_object(T* obj);
    
    std::array<std::unique_ptr<PoolShard<T>>, MAX_SHARDS> shards_;
    std::function<T*()> factory_;
    std::function<void(T*)> destructor_;
    
    mutable std::mutex stats_mutex_;
    ObjectPoolStats stats_;
};

// RAII wrapper for pooled objects
template<typename T>
class PooledObject {
public:
    PooledObject() : pool_(nullptr), object_(nullptr) {}
    
    explicit PooledObject(ObjectPool<T>* pool)
        : pool_(pool)
        , object_(pool ? pool->acquire() : nullptr) {
        if (!object_ && pool_) {
            object_ = new T();
        }
    }
    
    ~PooledObject() {
        reset();
    }
    
    void reset() {
        if (object_ && pool_) {
            pool_->release(object_);
            object_ = nullptr;
        } else if (object_) {
            delete object_;
            object_ = nullptr;
        }
    }
    
    T* get() const { return object_; }
    T* operator->() const { return object_; }
    T& operator*() const { return *object_; }
    explicit operator bool() const { return object_ != nullptr; }
    
    // Disable copy
    PooledObject(const PooledObject&) = delete;
    PooledObject& operator=(const PooledObject&) = delete;
    
    // Enable move
    PooledObject(PooledObject&& other) noexcept
        : pool_(other.pool_), object_(other.object_) {
        other.object_ = nullptr;
    }
    
    PooledObject& operator=(PooledObject&& other) noexcept {
        if (this != &other) {
            reset();
            pool_ = other.pool_;
            object_ = other.object_;
            other.object_ = nullptr;
        }
        return *this;
    }
    
private:
    ObjectPool<T>* pool_;
    T* object_;
};

} // namespace memory
} // namespace best_server

#endif // BEST_SERVER_MEMORY_OBJECT_POOL_HPP