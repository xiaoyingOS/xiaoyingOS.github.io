// FileDescriptorPool implementation
#include "best_server/io/file_descriptor_pool.hpp"
#include <stdexcept>
#include <ctime>
#include <algorithm>

namespace best_server {
namespace io {

FileDescriptorPool::FileDescriptorPool() {
}

FileDescriptorPool::~FileDescriptorPool() {
    std::lock_guard<std::mutex> lock(mutex_);
    
    // 关闭所有打开的文件描述符
    for (auto& [path, info] : fd_map_) {
        if (info.fd >= 0) {
            ::close(info.fd);
        }
    }
    
    fd_map_.clear();
}

int FileDescriptorPool::open_file(const std::string& path) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    // 检查文件是否已打开
    auto it = fd_map_.find(path);
    if (it != fd_map_.end()) {
        // 文件已打开，增加引用计数
        it->second.ref_count++;
        return it->second.fd;
    }
    
    // 打开文件
    int fd = ::open(path.c_str(), O_RDONLY | O_NONBLOCK);
    if (fd < 0) {
        return -1;
    }
    
    // 获取文件大小
    struct stat st;
    if (fstat(fd, &st) < 0) {
        ::close(fd);
        return -1;
    }
    
    // 添加到池中
    FileDescriptorInfo info;
    info.fd = fd;
    info.ref_count = 1;
    info.path = path;
    info.file_size = st.st_size;
    info.open_time = std::time(nullptr);
    
    fd_map_.emplace(path, std::move(info));
    
    return fd;
}

void FileDescriptorPool::close_file(const std::string& path) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto it = fd_map_.find(path);
    if (it == fd_map_.end()) {
        return;  // 文件未打开
    }
    
    // 减少引用计数
    int new_count = --it->second.ref_count;
    
    if (new_count <= 0) {
        // 引用计数为0，关闭文件描述符
        if (it->second.fd >= 0) {
            ::close(it->second.fd);
        }
        fd_map_.erase(it);
    }
}

size_t FileDescriptorPool::get_file_size(const std::string& path) const {
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto it = fd_map_.find(path);
    if (it != fd_map_.end()) {
        return it->second.file_size;
    }
    
    // 文件未打开，直接获取大小
    struct stat st;
    if (stat(path.c_str(), &st) < 0) {
        return 0;
    }
    
    return st.st_size;
}

bool FileDescriptorPool::is_file_open(const std::string& path) const {
    std::lock_guard<std::mutex> lock(mutex_);
    return fd_map_.find(path) != fd_map_.end();
}

int FileDescriptorPool::get_ref_count(const std::string& path) const {
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto it = fd_map_.find(path);
    if (it != fd_map_.end()) {
        return it->second.ref_count;
    }
    
    return 0;
}

void FileDescriptorPool::cleanup_idle_descriptors(int idle_seconds) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    time_t now = std::time(nullptr);
    
    auto it = fd_map_.begin();
    while (it != fd_map_.end()) {
        // 只清理引用计数为0的文件
        if (it->second.ref_count == 0) {
            // 检查是否超过空闲时间
            if (now - it->second.open_time > idle_seconds) {
                // 关闭文件描述符
                if (it->second.fd >= 0) {
                    ::close(it->second.fd);
                }
                it = fd_map_.erase(it);
                continue;
            }
        }
        ++it;
    }
}

size_t FileDescriptorPool::get_open_file_count() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return fd_map_.size();
}

size_t FileDescriptorPool::get_total_ref_count() const {
    std::lock_guard<std::mutex> lock(mutex_);
    
    size_t total = 0;
    for (const auto& [path, info] : fd_map_) {
        total += info.ref_count;
    }
    
    return total;
}

// 全局单例
FileDescriptorPool& get_global_file_descriptor_pool() {
    static FileDescriptorPool pool;
    return pool;
}

} // namespace io
} // namespace best_server