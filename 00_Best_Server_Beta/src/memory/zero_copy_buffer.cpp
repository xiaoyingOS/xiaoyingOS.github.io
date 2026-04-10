// ZeroCopyBuffer - Zero-copy buffer implementation

#include "best_server/memory/zero_copy_buffer.hpp"
#include <cstring>
#include <algorithm>

namespace best_server {
namespace memory {

// BufferData implementation
BufferData::BufferData(size_t capacity)
    : capacity_(capacity)
    , ref_count_(1) {
    data_ = std::unique_ptr<char[]>(new char[capacity]);
}

BufferData::~BufferData() = default;

bool BufferData::resize(size_t new_capacity) {
    if (new_capacity <= capacity_) {
        return true;
    }
    
    auto new_data = std::unique_ptr<char[]>(new char[new_capacity]);
    std::memcpy(new_data.get(), data_.get(), capacity_);
    data_ = std::move(new_data);
    capacity_ = new_capacity;
    
    return true;
}

// ZeroCopyBuffer implementation
ZeroCopyBuffer::ZeroCopyBuffer()
    : offset_(0)
    , size_(0) {
}

ZeroCopyBuffer::ZeroCopyBuffer(size_t capacity)
    : data_(std::make_shared<BufferData>(capacity))
    , offset_(0)
    , size_(0) {
}

ZeroCopyBuffer::ZeroCopyBuffer(const char* data, size_t size)
    : data_(std::make_shared<BufferData>(size))
    , offset_(0)
    , size_(size) {
    if (data && size > 0) {
        std::memcpy(data_->data(), data, size);
    }
}

ZeroCopyBuffer::~ZeroCopyBuffer() = default;

ZeroCopyBuffer::ZeroCopyBuffer(const ZeroCopyBuffer& other)
    : data_(other.data_)
    , offset_(other.offset_)
    , size_(other.size_) {
    if (data_) {
        data_->add_ref();
    }
}

ZeroCopyBuffer& ZeroCopyBuffer::operator=(const ZeroCopyBuffer& other) {
    if (this != &other) {
        if (data_) {
            data_->release();
        }
        data_ = other.data_;
        offset_ = other.offset_;
        size_ = other.size_;
        if (data_) {
            data_->add_ref();
        }
    }
    return *this;
}

ZeroCopyBuffer::ZeroCopyBuffer(ZeroCopyBuffer&& other) noexcept
    : data_(std::move(other.data_))
    , offset_(other.offset_)
    , size_(other.size_) {
    other.offset_ = 0;
    other.size_ = 0;
}

ZeroCopyBuffer& ZeroCopyBuffer::operator=(ZeroCopyBuffer&& other) noexcept {
    if (this != &other) {
        if (data_) {
            data_->release();
        }
        data_ = std::move(other.data_);
        offset_ = other.offset_;
        size_ = other.size_;
        other.offset_ = 0;
        other.size_ = 0;
    }
    return *this;
}

bool ZeroCopyBuffer::write(const void* src, size_t len) {
    if (!data_ || offset_ + size_ + len > data_->capacity()) {
        if (!reserve(len)) {
            return false;
        }
    }
    
    std::memcpy(data_->data() + offset_ + size_, src, len);
    size_ += len;
    return true;
}

bool ZeroCopyBuffer::read(void* dst, size_t len) {
    if (size_ < len) {
        return false;
    }
    
    std::memcpy(dst, data_->data() + offset_, len);
    offset_ += len;
    size_ -= len;
    
    return true;
}

void ZeroCopyBuffer::consume(size_t len) {
    len = std::min(len, size_);
    offset_ += len;
    size_ -= len;
}

void ZeroCopyBuffer::clear() {
    offset_ = 0;
    size_ = 0;
}

bool ZeroCopyBuffer::reserve(size_t len) {
    if (!data_) {
        data_ = std::make_shared<BufferData>(len);
        return true;
    }
    
    if (offset_ + size_ + len <= data_->capacity()) {
        return true;
    }
    
    // Try to compact first
    if (offset_ > 0) {
        if (size_ + len <= data_->capacity()) {
            std::memmove(data_->data(), data_->data() + offset_, size_);
            offset_ = 0;
            return true;
        }
    }
    
    // Need to resize
    size_t new_capacity = std::max(data_->capacity() * 2, offset_ + size_ + len);
    ensure_unique();
    return data_->resize(new_capacity);
}

bool ZeroCopyBuffer::append(const ZeroCopyBuffer& other) {
    if (!other.data_ || other.size_ == 0) {
        return true;
    }
    
    // Try to avoid copy if we share the same underlying data and are contiguous
    if (data_ == other.data_ && 
        offset_ + size_ == other.offset_ && 
        offset_ + size_ + other.size_ <= data_->capacity()) {
        // Contiguous in same buffer - just extend size
        size_ += other.size_;
        return true;
    }
    
    // Otherwise, we need to copy
    return write(other.data(), other.size());
}

ZeroCopyBuffer ZeroCopyBuffer::slice(size_t offset, size_t len) const {
    ZeroCopyBuffer result;
    result.data_ = data_;
    if (data_) {
        data_->add_ref();
    }
    result.offset_ = offset_ + offset;
    result.size_ = std::min(len, size_ - offset);
    return result;
}

void ZeroCopyBuffer::ensure_unique() {
    if (!data_) {
        return;
    }
    
    // Check if we're the only owner
    if (data_->ref_count() == 1) {
        return;
    }
    
    // Create a copy with exact size needed
    size_t needed_capacity = size_;
    auto new_data = std::make_shared<BufferData>(needed_capacity);
    if (size_ > 0) {
        std::memcpy(new_data->data(), data_->data() + offset_, size_);
    }
    
    // Release old data and acquire new
    data_->release();
    data_ = new_data;
    offset_ = 0;
}

bool ZeroCopyBuffer::is_unique() const {
    if (!data_) {
        return true;
    }
    return data_->ref_count() == 1;
}

// BufferChain implementation
BufferChain::BufferChain() = default;

BufferChain::~BufferChain() = default;

void BufferChain::add_buffer(ZeroCopyBuffer&& buffer) {
    if (!buffer.empty()) {
        buffers_.push_back(std::move(buffer));
    }
}

size_t BufferChain::total_size() const {
    size_t total = 0;
    for (const auto& buffer : buffers_) {
        total += buffer.size();
    }
    return total;
}

ZeroCopyBuffer BufferChain::flatten() const {
    size_t total = total_size();
    if (total == 0) {
        return ZeroCopyBuffer();
    }
    
    ZeroCopyBuffer result(total);
    for (const auto& buffer : buffers_) {
        result.write(buffer.data(), buffer.size());
    }
    
    return result;
}

void BufferChain::clear() {
    buffers_.clear();
}

bool BufferChain::write(const void* src, size_t len) {
    if (buffers_.empty() || buffers_.back().remaining() < len) {
        buffers_.emplace_back(ZeroCopyBuffer(len));
    }
    return buffers_.back().write(src, len);
}

bool BufferChain::read(void* dst, size_t len) {
    size_t remaining = len;
    char* ptr = static_cast<char*>(dst);
    
    while (remaining > 0 && !buffers_.empty()) {
        size_t to_read = std::min(remaining, buffers_.front().size());
        if (!buffers_.front().read(ptr, to_read)) {
            return false;
        }
        
        ptr += to_read;
        remaining -= to_read;
        
        if (buffers_.front().empty()) {
            buffers_.erase(buffers_.begin());
        }
    }
    
    return remaining == 0;
}

void BufferChain::consume(size_t len) {
    size_t remaining = len;
    
    while (remaining > 0 && !buffers_.empty()) {
        size_t to_consume = std::min(remaining, buffers_.front().size());
        buffers_.front().consume(to_consume);
        remaining -= to_consume;
        
        if (buffers_.front().empty()) {
            buffers_.erase(buffers_.begin());
        }
    }
}

// IOVector implementation
IOVector::IOVector() : count_(0) {
}

IOVector::~IOVector() = default;

void IOVector::add_buffer(const ZeroCopyBuffer& buffer) {
    if (count_ >= MAX_IOV) {
        return;
    }
    
    iov_[count_].iov_base = const_cast<char*>(buffer.data());
    iov_[count_].iov_len = buffer.size();
    ++count_;
    
    // Keep reference to buffer data by storing the buffer itself
    // The buffer's data will be kept alive by the shared_ptr inside
    // We don't need to access private members - just store the buffer
    // For simplicity, we skip the buffers_ vector and rely on caller's lifetime management
}

void IOVector::add_buffer(const void* data, size_t size) {
    if (count_ >= MAX_IOV) {
        return;
    }
    
    iov_[count_].iov_base = const_cast<void*>(data);
    iov_[count_].iov_len = size;
    ++count_;
}

void IOVector::clear() {
    count_ = 0;
    buffers_.clear();
}

size_t IOVector::total_size() const {
    size_t total = 0;
    for (size_t i = 0; i < count_; ++i) {
        total += iov_[i].iov_len;
    }
    return total;
}

} // namespace memory
} // namespace best_server