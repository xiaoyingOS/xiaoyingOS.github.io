// FileDescriptorPool - 文件描述符池
//
// 功能：
// - 共享文件描述符，避免重复打开同一文件
// - 引用计数管理
// - 自动关闭无人使用的文件描述符

#ifndef BEST_SERVER_IO_FILE_DESCRIPTOR_POOL_HPP
#define BEST_SERVER_IO_FILE_DESCRIPTOR_POOL_HPP

#include <string>
#include <unordered_map>
#include <mutex>
#include <atomic>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

namespace best_server {
namespace io {

// 文件描述符信息
struct FileDescriptorInfo {
    int fd;                      // 文件描述符
    int ref_count;               // 引用计数
    std::string path;            // 文件路径
    size_t file_size;            // 文件大小
    time_t open_time;            // 打开时间
};

// 文件描述符池
class FileDescriptorPool {
public:
    FileDescriptorPool();
    ~FileDescriptorPool();
    
    // 打开文件（共享文件描述符）
    // 返回值：文件描述符，失败返回-1
    int open_file(const std::string& path);
    
    // 关闭文件（减少引用计数）
    void close_file(const std::string& path);
    
    // 获取文件大小
    size_t get_file_size(const std::string& path) const;
    
    // 检查文件是否已打开
    bool is_file_open(const std::string& path) const;
    
    // 获取引用计数
    int get_ref_count(const std::string& path) const;
    
    // 清理空闲文件描述符（超过指定时间未使用）
    void cleanup_idle_descriptors(int idle_seconds = 300);
    
    // 获取统计信息
    size_t get_open_file_count() const;
    size_t get_total_ref_count() const;
    
private:
    mutable std::mutex mutex_;
    std::unordered_map<std::string, FileDescriptorInfo> fd_map_;
    
    // 禁止拷贝
    FileDescriptorPool(const FileDescriptorPool&) = delete;
    FileDescriptorPool& operator=(const FileDescriptorPool&) = delete;
};

// 全局单例
FileDescriptorPool& get_global_file_descriptor_pool();

} // namespace io
} // namespace best_server

#endif // BEST_SERVER_IO_FILE_DESCRIPTOR_POOL_HPP