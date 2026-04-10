// FolderTransfer implementation - Ultra-high performance folder transfer
#include "best_server/io/folder_transfer.hpp"
#include <iostream>
#include <algorithm>
#include <cstring>
#include <errno.h>

namespace best_server {
namespace io {

namespace fs = std::filesystem;

// ==================== FolderTransfer Implementation ====================

FolderTransfer::FolderTransfer(const FolderTransferConfig& config)
    : config_(config)
    , stats_()
{
    stats_.start_time = std::chrono::steady_clock::now();
    transfer_gate_ = std::make_unique<Gate>(config_.concurrency_limit);
}

FolderTransfer::~FolderTransfer() {
    stop();
}

// BFS并行遍历工作线程
void FolderTransfer::bfs_worker_thread() {
    while (running_.load()) {
        if (paused_.load()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            continue;
        }
        
        std::string current_path;
        bool has_task = false;
        
        {
            std::unique_lock<std::mutex> lock(bfs_mutex_);
            bfs_cv_.wait(lock, [this]() {
                return !bfs_queue_.empty() || bfs_complete_.load() || !running_.load();
            });
            
            if (!running_.load()) break;
            
            if (!bfs_queue_.empty()) {
                current_path = bfs_queue_.front();
                bfs_queue_.pop();
                has_task = true;
            }
        }
        
        if (has_task) {
            std::error_code ec;
            fs::directory_iterator it(current_path, ec);
            if (ec) {
                stats_.errors++;
                continue;
            }
            
            for (const auto& entry : it) {
                std::string entry_path = entry.path().string();
                
                if (entry.is_directory(ec)) {
                    // 添加目录到BFS队列（供其他线程处理）
                    {
                        std::lock_guard<std::mutex> lock(bfs_mutex_);
                        bfs_queue_.push(entry_path);
                    }
                    bfs_cv_.notify_one();
                    stats_.directories++;
                } else {
                    // 添加文件任务到传输队列
                    size_t file_size = fs::file_size(entry_path, ec);
                    if (!ec) {
                        std::string relative_path = entry_path.substr(current_path.find_last_of('/') + 1);
                        FileTransferTask task(entry_path, entry_path, file_size, false);
                        
                        {
                            std::lock_guard<std::mutex> lock(queue_mutex_);
                            task_queue_.push(task);
                        }
                        queue_cv_.notify_one();
                        
                        stats_.total_files++;
                        stats_.total_bytes += file_size;
                    } else {
                        stats_.errors++;
                    }
                }
            }
        }
    }
}

// BFS并行遍历文件夹（多线程并行遍历不同子目录）
void FolderTransfer::parallel_bfs_traverse(const std::string& path, const std::string& /*base_path*/) {
    // 将根目录加入BFS队列
    {
        std::lock_guard<std::mutex> lock(bfs_mutex_);
        bfs_queue_.push(path);
    }
    bfs_cv_.notify_all();
    
    // 等待BFS遍历完成
    while (true) {
        std::unique_lock<std::mutex> lock(bfs_mutex_);
        if (bfs_queue_.empty() && bfs_complete_.load()) {
            break;
        }
        lock.unlock();
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
}

// 创建目录
bool FolderTransfer::create_directory(const std::string& path) {
    std::error_code ec;
    return fs::create_directories(path, ec);
}

// 使用sendfile零拷贝传输大文件（最高性能）
bool FolderTransfer::transfer_file_sendfile(const std::string& src, const std::string& dst, size_t size) {
    int src_fd = open(src.c_str(), O_RDONLY);
    if (src_fd < 0) {
        return false;
    }
    
    int dst_fd = open(dst.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (dst_fd < 0) {
        close(src_fd);
        return false;
    }
    
    // 设置目标文件大小
    if (ftruncate(dst_fd, size) < 0) {
        close(src_fd);
        close(dst_fd);
        return false;
    }
    
    // 使用sendfile零拷贝传输（内核态直接传输，不经过用户态）
    size_t total_sent = 0;
    off_t offset = 0;
    
    while (total_sent < size && !paused_.load()) {
        ssize_t sent = sendfile(dst_fd, src_fd, &offset, size - total_sent);
        if (sent <= 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
                continue;
            }
            close(src_fd);
            close(dst_fd);
            return false;
        }
        total_sent += sent;
        stats_.transferred_bytes += sent;
        
        // 回调进度
        if (progress_callback_ && total_sent % (1024 * 1024) == 0) {
            progress_callback_(stats_);
        }
    }
    
    close(src_fd);
    close(dst_fd);
    
    if (total_sent == size) {
    }
    
    return total_sent == size;
}

// 刷新小文件批量缓冲区
void FolderTransfer::flush_small_file_batch() {
    std::lock_guard<std::mutex> lock(batch_mutex_);
    
    if (small_file_batch_.empty()) {
        return;
    }
    
    // 批量写入所有小文件
    for (const auto& file : small_file_batch_.files) {
        int fd = open(file.path.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
        if (fd >= 0) {
            write(fd, small_file_batch_.buffer.data() + file.offset, file.size);
            close(fd);
            stats_.transferred_bytes += file.size;
        }
    }
    
    small_file_batch_.clear();
}

// 小文件批量传输
bool FolderTransfer::transfer_small_file_batch(const FileTransferTask& task) {
    // 读取文件内容
    int src_fd = open(task.source_path.c_str(), O_RDONLY);
    if (src_fd < 0) {
        return false;
    }
    
    std::vector<char> buffer(task.file_size);
    ssize_t bytes_read = read(src_fd, buffer.data(), task.file_size);
    close(src_fd);
    
    if (bytes_read <= 0) {
        return false;
    }
    
    // 尝试添加到批量缓冲区
    {
        std::lock_guard<std::mutex> lock(batch_mutex_);
        if (!small_file_batch_.can_add(task.file_size)) {
            // 缓冲区已满，先刷新
            lock.~lock_guard();
            flush_small_file_batch();
        } else {
            // 创建目标文件路径
            std::string dest_path = task.dest_path;
            std::string dest_dir = dest_path.substr(0, dest_path.find_last_of('/'));
            create_directory(dest_dir);
            
            // 添加到批量缓冲区
            small_file_batch_.add(dest_path, buffer.data(), task.file_size);
            return true;
        }
    }
    
    // 直接写入文件
    int dst_fd = open(task.dest_path.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (dst_fd < 0) {
        return false;
    }
    
    ssize_t bytes_written = write(dst_fd, buffer.data(), bytes_read);
    close(dst_fd);
    
    if (bytes_written == bytes_read) {
        stats_.transferred_bytes += bytes_written;
        return true;
    }
    
    return false;
}

// 使用mmap传输大文件
bool FolderTransfer::transfer_file_mmap(const std::string& src, const std::string& dst, size_t size) {
    int src_fd = open(src.c_str(), O_RDONLY);
    if (src_fd < 0) {
        return false;
    }
    
    int dst_fd = open(dst.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (dst_fd < 0) {
        close(src_fd);
        return false;
    }
    
    // 设置目标文件大小
    if (ftruncate(dst_fd, size) < 0) {
        close(src_fd);
        close(dst_fd);
        return false;
    }
    
    // mmap源文件和目标文件
    void* src_map = mmap(nullptr, size, PROT_READ, MAP_PRIVATE, src_fd, 0);
    if (src_map == MAP_FAILED) {
        close(src_fd);
        close(dst_fd);
        return false;
    }
    
    void* dst_map = mmap(nullptr, size, PROT_WRITE, MAP_SHARED, dst_fd, 0);
    if (dst_map == MAP_FAILED) {
        munmap(src_map, size);
        close(src_fd);
        close(dst_fd);
        return false;
    }
    
    // 内存拷贝（零拷贝读取，一次性拷贝）
    memcpy(dst_map, src_map, size);
    
    // 同步到磁盘
    msync(dst_map, size, MS_SYNC);
    
    munmap(src_map, size);
    munmap(dst_map, size);
    close(src_fd);
    close(dst_fd);
    
    return true;
}

// 使用read/write传输文件（分块）
bool FolderTransfer::transfer_file_readwrite(const std::string& src, const std::string& dst, size_t size) {
    int src_fd = open(src.c_str(), O_RDONLY);
    if (src_fd < 0) {
        return false;
    }
    
    int dst_fd = open(dst.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (dst_fd < 0) {
        close(src_fd);
        return false;
    }
    
    // 从内存池获取缓冲区
    void* buffer = buffer_pool_.acquire();
    size_t chunk_size = config_.chunk_size;
    
    size_t total_transferred = 0;
    
    while (total_transferred < size && !paused_.load()) {
        size_t to_read = std::min(chunk_size, size - total_transferred);
        
        ssize_t bytes_read = read(src_fd, buffer, to_read);
        if (bytes_read <= 0) break;
        
        ssize_t bytes_written = write(dst_fd, buffer, bytes_read);
        if (bytes_written != bytes_read) {
            buffer_pool_.release(buffer);
            close(src_fd);
            close(dst_fd);
            return false;
        }
        
        total_transferred += bytes_written;
        stats_.transferred_bytes += bytes_written;
        
        // 回调进度
        if (progress_callback_ && total_transferred % (1024 * 1024) == 0) { // 每MB回调一次
            progress_callback_(stats_);
        }
    }
    
    buffer_pool_.release(buffer);
    close(src_fd);
    close(dst_fd);
    
    return total_transferred == size;
}

// 传输单个文件（根据大小选择策略）
bool FolderTransfer::transfer_file(const FileTransferTask& task) {
    if (task.is_directory) {
        return create_directory(task.dest_path);
    }
    
    // 通过Gate控制并发
    transfer_gate_->enter();
    
    bool success = false;
    
    try {
        // 检查源文件是否存在
        if (!fs::exists(task.source_path)) {
            stats_.errors++;
            transfer_gate_->exit();
            return false;
        }
        
        // 根据文件大小选择传输策略
        if (config_.use_sendfile && task.file_size >= config_.sendfile_threshold) {
            // 大文件使用sendfile零拷贝（最快）
            success = transfer_file_sendfile(task.source_path, task.dest_path, task.file_size);
        } else if (task.file_size <= config_.small_file_threshold) {
            // 小文件批量传输
            success = transfer_small_file_batch(task);
        } else {
            // 中等文件使用mmap或read/write
            success = transfer_file_readwrite(task.source_path, task.dest_path, task.file_size);
        }
        
        if (!success) {
            stats_.errors++;
        }
        
        // 回调进度
        if (progress_callback_) {
            progress_callback_(stats_);
        }
    } catch (...) {
        stats_.errors++;
        success = false;
    }
    
    transfer_gate_->exit();
    return success;
}

// 传输工作线程
void FolderTransfer::transfer_worker_thread() {
    while (running_.load()) {
        if (paused_.load()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            continue;
        }
        
        FileTransferTask task("", "", 0, false);
        bool has_task = false;
        
        {
            std::unique_lock<std::mutex> lock(queue_mutex_);
            queue_cv_.wait(lock, [this]() {
                return !task_queue_.empty() || bfs_complete_.load() || !running_.load();
            });
            
            if (!running_.load()) break;
            
            if (!task_queue_.empty()) {
                task = task_queue_.front();
                task_queue_.pop();
                has_task = true;
            }
        }
        
        if (has_task) {
            transfer_file(task);
        }
    }
}

// 处理任务队列
void FolderTransfer::process_tasks() {
    running_.store(true);
    
    // 启动BFS遍历工作线程
    for (size_t i = 0; i < config_.traversal_thread_count; ++i) {
        bfs_worker_threads_.emplace_back([this]() {
            this->bfs_worker_thread();
        });
    }
    
    // 启动传输工作线程
    for (size_t i = 0; i < config_.transfer_thread_count; ++i) {
        transfer_worker_threads_.emplace_back([this]() {
            this->transfer_worker_thread();
        });
    }
}

// 上传文件夹
bool FolderTransfer::upload_folder(const std::string& local_folder, const std::string& /*remote_base_path*/,
                                   ProgressCallback progress_cb, CompletionCallback completion_cb) {
    progress_callback_ = progress_cb;
    completion_callback_ = completion_cb;
    
    std::error_code ec;
    if (!fs::exists(local_folder, ec)) {
        if (completion_cb) {
            completion_cb(false, "Source folder does not exist");
        }
        return false;
    }
    
    // 启动并行BFS遍历和传输
    process_tasks();
    
    // 开始BFS遍历
    parallel_bfs_traverse(local_folder, local_folder);
    
    // 标记BFS遍历完成
    bfs_complete_.store(true);
    bfs_cv_.notify_all();
    
    // 等待所有任务完成
    for (auto& thread : bfs_worker_threads_) {
        if (thread.joinable()) {
            thread.join();
        }
    }
    
    for (auto& thread : transfer_worker_threads_) {
        if (thread.joinable()) {
            thread.join();
        }
    }
    
    // 刷新剩余的小文件批量缓冲区
    flush_small_file_batch();
    
    bool success = (stats_.errors.load() == 0);
    
    if (completion_cb) {
        completion_cb(success, success ? "Upload completed" : "Upload completed with errors");
    }
    
    return success;
}

// 下载文件夹
bool FolderTransfer::download_folder(const std::string& remote_folder, const std::string& local_base_path,
                                     ProgressCallback progress_cb, CompletionCallback completion_cb) {
    progress_callback_ = progress_cb;
    completion_callback_ = completion_cb;
    
    std::error_code ec;
    if (!fs::exists(remote_folder, ec)) {
        if (completion_cb) {
            completion_cb(false, "Source folder does not exist");
        }
        return false;
    }
    
    // 创建本地目标目录
    create_directory(local_base_path);
    
    // 启动并行BFS遍历和传输
    process_tasks();
    
    // 开始BFS遍历
    parallel_bfs_traverse(remote_folder, remote_folder);
    
    // 标记BFS遍历完成
    bfs_complete_.store(true);
    bfs_cv_.notify_all();
    
    // 等待所有任务完成
    for (auto& thread : bfs_worker_threads_) {
        if (thread.joinable()) {
            thread.join();
        }
    }
    
    for (auto& thread : transfer_worker_threads_) {
        if (thread.joinable()) {
            thread.join();
        }
    }
    
    // 刷新剩余的小文件批量缓冲区
    flush_small_file_batch();
    
    bool success = (stats_.errors.load() == 0);
    
    if (completion_cb) {
        completion_cb(success, success ? "Download completed" : "Download completed with errors");
    }
    
    return success;
}

// 复制文件夹
bool FolderTransfer::copy_folder(const std::string& source, const std::string& destination,
                                 ProgressCallback progress_cb, CompletionCallback completion_cb) {
    return download_folder(source, destination, progress_cb, completion_cb);
}

// 停止传输
void FolderTransfer::stop() {
    running_.store(false);
    bfs_cv_.notify_all();
    queue_cv_.notify_all();
    
    for (auto& thread : bfs_worker_threads_) {
        if (thread.joinable()) {
            thread.join();
        }
    }
    bfs_worker_threads_.clear();
    
    for (auto& thread : transfer_worker_threads_) {
        if (thread.joinable()) {
            thread.join();
        }
    }
    transfer_worker_threads_.clear();
}

// 暂停传输
void FolderTransfer::pause() {
    paused_.store(true);
}

// 恢复传输
void FolderTransfer::resume() {
    paused_.store(false);
    bfs_cv_.notify_all();
    queue_cv_.notify_all();
}

// ==================== FolderArchiver Implementation ====================

// 获取文件夹大小（BFS遍历）
size_t FolderArchiver::get_folder_size(const std::string& path) {
    std::queue<std::string> bfs_queue;
    bfs_queue.push(path);
    
    size_t total_size = 0;
    std::error_code ec;
    
    while (!bfs_queue.empty()) {
        std::string current = bfs_queue.front();
        bfs_queue.pop();
        
        fs::directory_iterator it(current, ec);
        if (ec) continue;
        
        for (const auto& entry : it) {
            std::string entry_path = entry.path().string();
            
            if (entry.is_directory(ec)) {
                bfs_queue.push(entry_path);
            } else {
                size_t file_size = fs::file_size(entry_path, ec);
                if (!ec) {
                    total_size += file_size;
                }
            }
        }
    }
    
    return total_size;
}

// 获取文件夹统计
FolderArchiver::FolderInfo FolderArchiver::get_folder_info(const std::string& path) {
    FolderInfo info{};
    
    std::queue<std::string> bfs_queue;
    bfs_queue.push(path);
    
    std::error_code ec;
    
    while (!bfs_queue.empty()) {
        std::string current = bfs_queue.front();
        bfs_queue.pop();
        
        fs::directory_iterator it(current, ec);
        if (ec) continue;
        
        for (const auto& entry : it) {
            std::string entry_path = entry.path().string();
            
            if (entry.is_directory(ec)) {
                info.total_directories++;
                bfs_queue.push(entry_path);
            } else {
                size_t file_size = fs::file_size(entry_path, ec);
                if (!ec) {
                    info.total_files++;
                    info.total_bytes += file_size;
                    info.file_list.push_back(entry_path);
                }
            }
        }
    }
    
    return info;
}

// 压缩文件夹
bool FolderArchiver::compress_folder(const std::string& folder, const std::string& output_zip,
                                     ProgressCallback /*progress*/) {
    // 使用系统的 zip 命令（跨平台）
    std::string cmd = "cd \"" + folder + "\" && zip -r \"" + output_zip + "\" ./*";
    
    if (system(cmd.c_str()) == 0) {
        return true;
    }
    
    return false;
}

// 解压到文件夹
bool FolderArchiver::decompress_to_folder(const std::string& input_zip, const std::string& output_folder,
                                         ProgressCallback /*progress*/) {
    std::error_code ec;
    fs::create_directories(output_folder, ec);
    
    // 使用系统的 unzip 命令
    std::string cmd = "unzip -o \"" + input_zip + "\" -d \"" + output_folder + "\"";
    
    if (system(cmd.c_str()) == 0) {
        return true;
    }
    
    return false;
}

} // namespace io
} // namespace best_server
