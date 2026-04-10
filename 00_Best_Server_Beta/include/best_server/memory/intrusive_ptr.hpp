// Intrusive Reference Counting - Optimized reference counting
//
// Implements intrusive reference counting with:
// - Minimal overhead (no extra allocation)
// - Optimized atomic operations
// - Cache-line aligned counters
// - Support for weak references

#ifndef BEST_SERVER_MEMORY_INTRUSIVE_PTR_HPP
#define BEST_SERVER_MEMORY_INTRUSIVE_PTR_HPP

#include <atomic>
#include <cstdint>
#include <type_traits>

namespace best_server {
namespace memory {

// Intrusive reference count base class
// Classes that want to use intrusive_ptr should inherit from this
class RefCountedBase {
public:
    RefCountedBase() : ref_count_(1), weak_count_(0) {}
    
    // Add reference
    void add_ref() {
        ref_count_.fetch_add(1, std::memory_order_relaxed);
    }
    
    // Release reference
    void release() {
        if (ref_count_.fetch_sub(1, std::memory_order_acq_rel) == 1) {
            // No more strong references, destroy the object
            destroy();
        }
    }
    
    // Get reference count (for debugging)
    uint32_t ref_count() const {
        return ref_count_.load(std::memory_order_relaxed);
    }
    
    // Add weak reference
    void add_weak_ref() {
        weak_count_.fetch_add(1, std::memory_order_relaxed);
    }
    
    // Release weak reference
    void release_weak() {
        if (weak_count_.fetch_sub(1, std::memory_order_acq_rel) == 1) {
            // No more weak references, free the object
            deallocate();
        }
    }
    
    // Get weak count (for debugging)
    uint32_t weak_count() const {
        return weak_count_.load(std::memory_order_relaxed);
    }
    
protected:
    virtual ~RefCountedBase() = default;
    
    // Called when ref_count reaches 0
    virtual void destroy() {
        // Default implementation: just delete this
        delete this;
    }
    
    // Called when weak_count reaches 0
    virtual void deallocate() {
        // Default implementation: do nothing
        // Object is already destroyed
    }
    
private:
    alignas(64) std::atomic<uint32_t> ref_count_;
    alignas(64) std::atomic<uint32_t> weak_count_;
};

// Helper macro to make a class intrusive-able
#define BEST_SERVER_INTRUSIVE_PTR_SUPPORT \
    mutable std::atomic<uint32_t> ref_count_{1}; \
    void add_ref() const { ref_count_.fetch_add(1, std::memory_order_relaxed); } \
    void release() const { \
        if (ref_count_.fetch_sub(1, std::memory_order_acq_rel) == 1) { \
            delete this; \
        } \
    } \
    uint32_t ref_count() const { return ref_count_.load(std::memory_order_relaxed); }

// Intrusive pointer
template<typename T>
class intrusive_ptr {
public:
    // Default constructor
    intrusive_ptr() noexcept : ptr_(nullptr) {}
    
    // nullptr constructor
    intrusive_ptr(std::nullptr_t) noexcept : ptr_(nullptr) {}
    
    // Constructor from raw pointer
    explicit intrusive_ptr(T* ptr) noexcept : ptr_(ptr) {}
    
    // Copy constructor
    intrusive_ptr(const intrusive_ptr& other) noexcept : ptr_(other.ptr_) {
        if (ptr_) {
            ptr_->add_ref();
        }
    }
    
    // Move constructor
    intrusive_ptr(intrusive_ptr&& other) noexcept : ptr_(other.ptr_) {
        other.ptr_ = nullptr;
    }
    
    // Destructor
    ~intrusive_ptr() {
        if (ptr_) {
            ptr_->release();
        }
    }
    
    // Copy assignment
    intrusive_ptr& operator=(const intrusive_ptr& other) noexcept {
        if (this != &other) {
            if (other.ptr_) {
                other.ptr_->add_ref();
            }
            if (ptr_) {
                ptr_->release();
            }
            ptr_ = other.ptr_;
        }
        return *this;
    }
    
    // Move assignment
    intrusive_ptr& operator=(intrusive_ptr&& other) noexcept {
        if (this != &other) {
            if (ptr_) {
                ptr_->release();
            }
            ptr_ = other.ptr_;
            other.ptr_ = nullptr;
        }
        return *this;
    }
    
    // nullptr assignment
    intrusive_ptr& operator=(std::nullptr_t) noexcept {
        if (ptr_) {
            ptr_->release();
            ptr_ = nullptr;
        }
        return *this;
    }
    
    // Get raw pointer
    T* get() const noexcept { return ptr_; }
    
    // Dereference
    T& operator*() const noexcept { return *ptr_; }
    T* operator->() const noexcept { return ptr_; }
    
    // Check if not null
    explicit operator bool() const noexcept { return ptr_ != nullptr; }
    
    // Reset to nullptr
    void reset() noexcept {
        if (ptr_) {
            ptr_->release();
            ptr_ = nullptr;
        }
    }
    
    // Reset to new pointer
    void reset(T* ptr) noexcept {
        if (ptr_) {
            ptr_->release();
        }
        ptr_ = ptr;
    }
    
    // Swap
    void swap(intrusive_ptr& other) noexcept {
        T* tmp = ptr_;
        ptr_ = other.ptr_;
        other.ptr_ = tmp;
    }
    
    // Get reference count
    uint32_t use_count() const noexcept {
        return ptr_ ? ptr_->ref_count() : 0;
    }
    
    // Comparison operators
    bool operator==(const intrusive_ptr& other) const noexcept {
        return ptr_ == other.ptr_;
    }
    
    bool operator!=(const intrusive_ptr& other) const noexcept {
        return ptr_ != other.ptr_;
    }
    
    bool operator<(const intrusive_ptr& other) const noexcept {
        return ptr_ < other.ptr_;
    }
    
    bool operator==(std::nullptr_t) const noexcept {
        return ptr_ == nullptr;
    }
    
    bool operator!=(std::nullptr_t) const noexcept {
        return ptr_ != nullptr;
    }
    
private:
    T* ptr_;
};

// Non-member swap
template<typename T>
void swap(intrusive_ptr<T>& lhs, intrusive_ptr<T>& rhs) noexcept {
    lhs.swap(rhs);
}

// make_intrusive (like make_shared)
template<typename T, typename... Args>
intrusive_ptr<T> make_intrusive(Args&&... args) {
    static_assert(std::is_base_of<RefCountedBase, T>::value || 
                  (requires(T t) { t.add_ref(); t.release(); t.ref_count(); }),
                  "T must support intrusive reference counting");
    return intrusive_ptr<T>(new T(std::forward<Args>(args)...));
}

// static_pointer_cast
template<typename T, typename U>
intrusive_ptr<T> static_pointer_cast(const intrusive_ptr<U>& r) noexcept {
    return intrusive_ptr<T>(static_cast<T*>(r.get()));
}

// dynamic_pointer_cast
template<typename T, typename U>
intrusive_ptr<T> dynamic_pointer_cast(const intrusive_ptr<U>& r) noexcept {
    return intrusive_ptr<T>(dynamic_cast<T*>(r.get()));
}

// const_pointer_cast
template<typename T, typename U>
intrusive_ptr<T> const_pointer_cast(const intrusive_ptr<U>& r) noexcept {
    return intrusive_ptr<T>(const_cast<T*>(r.get()));
}

// Intrusive weak pointer
template<typename T>
class intrusive_weak_ptr {
public:
    // Default constructor
    intrusive_weak_ptr() noexcept : ptr_(nullptr) {}
    
    // nullptr constructor
    intrusive_weak_ptr(std::nullptr_t) noexcept : ptr_(nullptr) {}
    
    // Constructor from intrusive_ptr
    intrusive_weak_ptr(const intrusive_ptr<T>& other) noexcept : ptr_(other.get()) {
        if (ptr_) {
            ptr_->add_weak_ref();
        }
    }
    
    // Copy constructor
    intrusive_weak_ptr(const intrusive_weak_ptr& other) noexcept : ptr_(other.ptr_) {
        if (ptr_) {
            ptr_->add_weak_ref();
        }
    }
    
    // Move constructor
    intrusive_weak_ptr(intrusive_weak_ptr&& other) noexcept : ptr_(other.ptr_) {
        other.ptr_ = nullptr;
    }
    
    // Destructor
    ~intrusive_weak_ptr() {
        if (ptr_) {
            ptr_->release_weak();
        }
    }
    
    // Copy assignment
    intrusive_weak_ptr& operator=(const intrusive_weak_ptr& other) noexcept {
        if (this != &other) {
            if (other.ptr_) {
                other.ptr_->add_weak_ref();
            }
            if (ptr_) {
                ptr_->release_weak();
            }
            ptr_ = other.ptr_;
        }
        return *this;
    }
    
    // Move assignment
    intrusive_weak_ptr& operator=(intrusive_weak_ptr&& other) noexcept {
        if (this != &other) {
            if (ptr_) {
                ptr_->release_weak();
            }
            ptr_ = other.ptr_;
            other.ptr_ = nullptr;
        }
        return *this;
    }
    
    // Assign from intrusive_ptr
    intrusive_weak_ptr& operator=(const intrusive_ptr<T>& other) noexcept {
        if (other.ptr_) {
            other.ptr_->add_weak_ref();
        }
        if (ptr_) {
            ptr_->release_weak();
        }
        ptr_ = other.ptr_;
        return *this;
    }
    
    // Lock to get strong reference
    intrusive_ptr<T> lock() const noexcept {
        // Try to add strong reference
        if (ptr_) {
            ptr_->add_ref();
            // Check if object is still alive
            if (ptr_->ref_count() > 0) {
                return intrusive_ptr<T>(ptr_);
            } else {
                // Object was destroyed, release the reference
                ptr_->release();
                return intrusive_ptr<T>();
            }
        }
        return intrusive_ptr<T>();
    }
    
    // Check if expired
    bool expired() const noexcept {
        return !ptr_ || ptr_->ref_count() == 0;
    }
    
    // Reset
    void reset() noexcept {
        if (ptr_) {
            ptr_->release_weak();
            ptr_ = nullptr;
        }
    }
    
    // Swap
    void swap(intrusive_weak_ptr& other) noexcept {
        T* tmp = ptr_;
        ptr_ = other.ptr_;
        other.ptr_ = tmp;
    }
    
    // Comparison operators
    bool operator==(const intrusive_weak_ptr& other) const noexcept {
        return ptr_ == other.ptr_;
    }
    
    bool operator!=(const intrusive_weak_ptr& other) const noexcept {
        return ptr_ != other.ptr_;
    }
    
    bool operator==(std::nullptr_t) const noexcept {
        return ptr_ == nullptr;
    }
    
    bool operator!=(std::nullptr_t) const noexcept {
        return ptr_ != nullptr;
    }
    
private:
    T* ptr_;
};

// Non-member swap for weak_ptr
template<typename T>
void swap(intrusive_weak_ptr<T>& lhs, intrusive_weak_ptr<T>& rhs) noexcept {
    lhs.swap(rhs);
}

} // namespace memory
} // namespace best_server

#endif // BEST_SERVER_MEMORY_INTRUSIVE_PTR_HPP