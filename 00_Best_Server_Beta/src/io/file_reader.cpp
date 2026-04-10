// FileReader - Asynchronous file reader implementation

#include "best_server/io/file_reader.hpp"
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <cstring>
#include <sys/mman.h>

namespace best_server {
namespace io {

// FileReader implementation
FileReader::FileReader()
    : fd_(-1)
    , file_size_(0)
    , position_(0)
    , event_loop_(nullptr)
    , mmap_enabled_(false)
    , read_ahead_size_(64 * 1024)
    , cache_size_(100) {
}

FileReader::~FileReader() {
    close();
}

bool FileReader::open(const std::string& path) {
    fd_ = ::open(path.c_str(), O_RDONLY | O_NONBLOCK);
    if (fd_ < 0) {
        return false;
    }
    
    path_ = path;
    
    struct stat st;
    if (fstat(fd_, &st) == 0) {
        file_size_ = st.st_size;
    }
    
    position_ = 0;
    ++stats_.open_files;
    
    return true;
}

void FileReader::close() {
    if (fd_ >= 0) {
        ::close(fd_);
        fd_ = -1;
    }
    
    cache_.clear();
}

void FileReader::read(size_t size, ReadCallback callback) {
    read_at(position_, size, std::move(callback));
}

void FileReader::read_at(uint64_t offset, size_t size, ReadCallback callback) {
    if (fd_ < 0) {
        callback(memory::ZeroCopyBuffer(), std::error_code(EBADF, std::system_category()));
        return;
    }
    
    memory::ZeroCopyBuffer buffer(size);
    
    if (mmap_enabled_ && offset + size <= file_size_) {
        // Use memory mapping
        void* mapped = mmap(nullptr, size, PROT_READ, MAP_PRIVATE, fd_, offset);
        if (mapped != MAP_FAILED) {
            std::memcpy(buffer.data(), mapped, size);
            munmap(mapped, size);
            
            stats_.bytes_read += size;
            ++stats_.read_count;
            ++stats_.cache_hits;
            
            callback(std::move(buffer), std::error_code());
            return;
        }
    }
    
    // Regular read
    ssize_t bytes_read = pread(fd_, buffer.data(), size, offset);
    
    if (bytes_read >= 0) {
        buffer.consume(bytes_read);
        stats_.bytes_read += bytes_read;
        ++stats_.read_count;
        ++stats_.cache_misses;
        
        callback(std::move(buffer), std::error_code());
    } else {
        callback(memory::ZeroCopyBuffer(), std::error_code(errno, std::system_category()));
    }
}

void FileReader::read_all(ReadCallback callback) {
    read_at(0, file_size_, std::move(callback));
}

void FileReader::prefetch(uint64_t offset, size_t size) {
    if (fd_ < 0) {
        return;
    }
    
    // Cache the prefetched data
    CacheEntry entry;
    entry.offset = offset;
    entry.data = memory::ZeroCopyBuffer(size);
    entry.last_access = 0;
    
    ssize_t bytes_read = pread(fd_, entry.data.data(), size, offset);
    if (bytes_read > 0) {
        entry.data.consume(bytes_read);
        cache_[offset] = std::move(entry);
        
        // Limit cache size
        if (cache_.size() > cache_size_) {
            // Remove oldest entry
            uint64_t oldest_offset = cache_.begin()->first;
            cache_.erase(oldest_offset);
        }
    }
}

uint64_t FileReader::size() const {
    return file_size_;
}

uint64_t FileReader::position() const {
    return position_;
}

bool FileReader::seek(uint64_t position) {
    if (position > file_size_) {
        return false;
    }
    
    position_ = position;
    return true;
}

void FileReader::enable_mmap(bool enable) {
    mmap_enabled_ = enable;
}

void FileReader::set_read_ahead_size(size_t size) {
    read_ahead_size_ = size;
}

void FileReader::handle_read_complete(memory::ZeroCopyBuffer&& buffer, std::error_code ec) {
    (void)buffer;
    (void)ec;
    // Called after async read completes
}

void FileReader::handle_read_at_complete(uint64_t offset, memory::ZeroCopyBuffer&& buffer, std::error_code ec) {
    (void)offset;
    (void)buffer;
    (void)ec;
    // Called after async read_at completes
}

// FileReaderPool implementation
FileReaderPool::FileReaderPool(size_t max_open_files)
    : event_loop_(nullptr)
    , max_open_files_(max_open_files) {
}

FileReaderPool::~FileReaderPool() = default;

std::shared_ptr<FileReader> FileReaderPool::get_reader(const std::string& path) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto it = readers_.find(path);
    if (it != readers_.end()) {
        // Move to end of LRU queue
        for (auto it2 = lru_queue_.begin(); it2 != lru_queue_.end(); ++it2) {
            if (*it2 == path) {
                lru_queue_.erase(it2);
                break;
            }
        }
        lru_queue_.push_back(path);
        
        return it->second;
    }
    
    // Check if we need to evict
    if (readers_.size() >= max_open_files_) {
        std::string oldest = lru_queue_.front();
        lru_queue_.pop_front();
        readers_.erase(oldest);
    }
    
    // Create new reader
    auto reader = std::make_shared<FileReader>();
    reader->set_event_loop(event_loop_);
    reader->open(path);
    
    readers_[path] = reader;
    lru_queue_.push_back(path);
    
    return reader;
}

void FileReaderPool::return_reader(const std::string& path) {
    (void)path;
    // Reader is kept in pool
}

void FileReaderPool::clear_cache() {
    std::lock_guard<std::mutex> lock(mutex_);
    readers_.clear();
    lru_queue_.clear();
}

size_t FileReaderPool::active_readers() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return readers_.size();
}

size_t FileReaderPool::cached_readers() const {
    return active_readers();
}

} // namespace io
} // namespace best_server