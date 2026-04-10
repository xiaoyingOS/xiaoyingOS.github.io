// FileWriter - Asynchronous file writer implementation

#include "best_server/io/file_writer.hpp"
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <cstring>

namespace best_server {
namespace io {

// FileWriter implementation
FileWriter::FileWriter()
    : fd_(-1)
    , position_(0)
    , event_loop_(nullptr)
    , batch_writes_enabled_(false)
    , batch_size_(64 * 1024)
    , sync_policy_(SyncPolicy::Never)
    , sync_interval_ms_(1000)
    , last_sync_time_(0) {
}

FileWriter::~FileWriter() {
    close();
}

bool FileWriter::open(const std::string& path, bool truncate) {
    int flags = O_WRONLY | O_CREAT | O_NONBLOCK;
    if (truncate) {
        flags |= O_TRUNC;
    }
    
    fd_ = ::open(path.c_str(), flags, 0644);
    if (fd_ < 0) {
        return false;
    }
    
    path_ = path;
    position_ = 0;
    ++stats_.open_files;
    
    return true;
}

void FileWriter::close() {
    flush();
    
    if (fd_ >= 0) {
        ::close(fd_);
        fd_ = -1;
    }
}

void FileWriter::write(memory::ZeroCopyBuffer&& data, WriteCallback callback) {
    if (fd_ < 0) {
        callback(0, std::error_code(EBADF, std::system_category()));
        return;
    }
    
    if (batch_writes_enabled_ && data.size() < batch_size_) {
        // Add to batch
        batch_buffer_.append(data);
        
        if (batch_buffer_.size() >= batch_size_) {
            flush_batch();
        }
        
        if (callback) {
            callback(data.size(), std::error_code());
        }
    } else {
        // Write immediately
        ssize_t bytes_written = pwrite(fd_, data.data(), data.size(), position_);
        
        if (bytes_written >= 0) {
            position_ += bytes_written;
            stats_.bytes_written += bytes_written;
            ++stats_.write_count;
            
            if (callback) {
                callback(bytes_written, std::error_code());
            }
        } else {
            if (callback) {
                callback(0, std::error_code(errno, std::system_category()));
            }
        }
    }
}

void FileWriter::write_at(uint64_t offset, memory::ZeroCopyBuffer&& data, WriteCallback callback) {
    if (fd_ < 0) {
        callback(0, std::error_code(EBADF, std::system_category()));
        return;
    }
    
    ssize_t bytes_written = pwrite(fd_, data.data(), data.size(), offset);
    
    if (bytes_written >= 0) {
        if (offset + bytes_written > position_) {
            position_ = offset + bytes_written;
        }
        stats_.bytes_written += bytes_written;
        ++stats_.write_count;
        
        if (callback) {
            callback(bytes_written, std::error_code());
        }
    } else {
        if (callback) {
            callback(0, std::error_code(errno, std::system_category()));
        }
    }
}

void FileWriter::append(memory::ZeroCopyBuffer&& data, WriteCallback callback) {
    write_at(position_, std::move(data), std::move(callback));
}

void FileWriter::flush(WriteCallback callback) {
    if (batch_writes_enabled_ && !batch_buffer_.empty()) {
        flush_batch();
    }
    
    if (sync_policy_ == SyncPolicy::OnWrite) {
        fsync(nullptr);
    }
    
    if (callback) {
        callback(stats_.bytes_written, std::error_code());
    }
}

void FileWriter::fsync(std::function<void(std::error_code)> callback) {
    if (fd_ < 0) {
        if (callback) {
            callback(std::error_code(EBADF, std::system_category()));
        }
        return;
    }
    
    int result = ::fsync(fd_);
    ++stats_.fsync_count;
    
    if (callback) {
        if (result == 0) {
            callback(std::error_code());
        } else {
            callback(std::error_code(errno, std::system_category()));
        }
    }
}

uint64_t FileWriter::position() const {
    return position_;
}

bool FileWriter::seek(uint64_t position) {
    position_ = position;
    return true;
}

void FileWriter::enable_batch_writes(bool enable, size_t batch_size) {
    batch_writes_enabled_ = enable;
    batch_size_ = batch_size;
}

void FileWriter::set_sync_policy(SyncPolicy policy, uint32_t interval_ms) {
    sync_policy_ = policy;
    sync_interval_ms_ = interval_ms;
}

void FileWriter::handle_write_complete(WriteCallback callback, size_t bytes_written, std::error_code ec) {
    (void)callback;
    (void)bytes_written;
    (void)ec;
    // Called after async write completes
}

void FileWriter::flush_batch() {
    if (batch_buffer_.empty()) {
        return;
    }
    
    ssize_t bytes_written = pwrite(fd_, batch_buffer_.data(), batch_buffer_.size(), position_);
    
    if (bytes_written >= 0) {
        position_ += bytes_written;
        stats_.bytes_written += bytes_written;
        ++stats_.write_count;
    }
    
    batch_buffer_.clear();
}

void FileWriter::handle_sync_complete(std::function<void(std::error_code)> callback, std::error_code ec) {
    (void)callback;
    (void)ec;
    // Called after async fsync completes
}

// FileWriterPool implementation
FileWriterPool::FileWriterPool(size_t max_open_files)
    : event_loop_(nullptr)
    , max_open_files_(max_open_files) {
}

FileWriterPool::~FileWriterPool() {
    flush_all();
}

std::shared_ptr<FileWriter> FileWriterPool::get_writer(const std::string& path) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto it = writers_.find(path);
    if (it != writers_.end()) {
        return it->second;
    }
    
    // Create new writer
    auto writer = std::make_shared<FileWriter>();
    writer->set_event_loop(event_loop_);
    writer->open(path);
    
    writers_[path] = writer;
    
    return writer;
}

void FileWriterPool::return_writer(const std::string& path) {
    (void)path;
    // Writer is kept in pool
}

void FileWriterPool::flush_all() {
    std::lock_guard<std::mutex> lock(mutex_);
    
    for (auto& pair : writers_) {
        pair.second->flush();
    }
}

size_t FileWriterPool::active_writers() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return writers_.size();
}

size_t FileWriterPool::pending_writes() const {
    size_t total = 0;
    
    std::lock_guard<std::mutex> lock(mutex_);
    for (const auto& pair : writers_) {
        total += pair.second->stats().pending_writes;
    }
    
    return total;
}

} // namespace io
} // namespace best_server