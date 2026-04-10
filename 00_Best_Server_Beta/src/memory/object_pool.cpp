// ObjectPool - High-performance object pool implementation

#include "best_server/memory/object_pool.hpp"
#include <cstring>

namespace best_server {
namespace memory {

// ObjectPool implementation
template<typename T>
ObjectPool<T>::ObjectPool(size_t shard_capacity)
    : factory_([]() { return new T(); })
    , destructor_([](T* obj) { delete obj; }) {
    
    for (size_t i = 0; i < MAX_SHARDS; ++i) {
        shards_[i] = std::make_unique<PoolShard<T>>(shard_capacity);
    }
}

template<typename T>
ObjectPool<T>::~ObjectPool() {
    clear();
}

template<typename T>
T* ObjectPool<T>::acquire() {
    int shard_id = get_shard_id();
    T* obj = shards_[shard_id]->acquire();
    
    if (obj) {
        std::lock_guard<std::mutex> lock(stats_mutex_);
        ++stats_.cache_hits;
        ++stats_.active_objects;
    } else {
        obj = create_object();
        if (obj) {
            std::lock_guard<std::mutex> lock(stats_mutex_);
            ++stats_.cache_misses;
            ++stats_.active_objects;
        }
    }
    
    return obj;
}

template<typename T>
void ObjectPool<T>::release(T* obj) {
    if (!obj) {
        return;
    }
    
    int shard_id = get_shard_id();
    
    if (shards_[shard_id]->release(obj)) {
        std::lock_guard<std::mutex> lock(stats_mutex_);
        --stats_.active_objects;
    } else {
        // Pool full, destroy object
        destroy_object(obj);
        std::lock_guard<std::mutex> lock(stats_mutex_);
        --stats_.active_objects;
    }
}

template<typename T>
ObjectPoolStats ObjectPool<T>::stats() const {
    std::lock_guard<std::mutex> lock(stats_mutex_);
    ObjectPoolStats s = stats_;
    
    size_t total_size = 0;
    for (size_t i = 0; i < MAX_SHARDS; ++i) {
        total_size += shards_[i]->size();
    }
    s.pool_size = total_size;
    
    return s;
}

template<typename T>
void ObjectPool<T>::set_factory(std::function<T*()> factory) {
    factory_ = factory;
}

template<typename T>
void ObjectPool<T>::set_destructor(std::function<void(T*)> destructor) {
    destructor_ = destructor;
}

template<typename T>
void ObjectPool<T>::resize(size_t shard_capacity) {
    for (size_t i = 0; i < MAX_SHARDS; ++i) {
        shards_[i] = std::make_unique<PoolShard<T>>(shard_capacity);
    }
}

template<typename T>
void ObjectPool<T>::clear() {
    for (size_t i = 0; i < MAX_SHARDS; ++i) {
        shards_[i] = std::make_unique<PoolShard<T>>(shards_[i]->capacity());
    }
    
    std::lock_guard<std::mutex> lock(stats_mutex_);
    stats_.active_objects = 0;
}

template<typename T>
int ObjectPool<T>::get_shard_id() const {
#if BEST_SERVER_PLATFORM_LINUX
    return sched_getcpu() % MAX_SHARDS;
#elif BEST_SERVER_PLATFORM_MACOS
    thread_port_t thread = pthread_mach_thread_np(pthread_self());
    return thread % MAX_SHARDS;
#else
    return 0;
#endif
}

template<typename T>
T* ObjectPool<T>::create_object() {
    return factory_();
}

template<typename T>
void ObjectPool<T>::destroy_object(T* obj) {
    if (obj && destructor_) {
        destructor_(obj);
    }
}

// Explicit template instantiations
template class ObjectPool<int>;
template class ObjectPool<char>;
template class ObjectPool<void*>;

} // namespace memory
} // namespace best_server