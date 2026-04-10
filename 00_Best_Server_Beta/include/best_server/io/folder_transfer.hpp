// FolderTransfer - Ultra-high performance folder transfer
//
// 实现最高性能的文件夹传输算法：
// - BFS并行遍历（多线程同时遍历不同子目录）
// - 多核并行遍历（利用CPU核心数）
// - sendfile零拷贝传输大文件（内核态直接传输）
// - 小文件批量合并DMA写入（减少系统调用）
// - Gate控制并发度（防止资源耗尽）
// - 内存池复用（减少分配开销）
// - NUMA感知优化（可选）

#ifndef BEST_SERVER_IO_FOLDER_TRANSFER_HPP
#define BEST_SERVER_IO_FOLDER_TRANSFER_HPP

#include <string>
#include <vector>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <thread>
#include <atomic>
#include <functional>
#include <filesystem>
#include <unordered_map>
#include <memory>
#include <deque>
#include <semaphore>
#include <cstdint>
#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/sendfile.h>
#include <cstring>
#include <algorithm>

namespace best_server {
namespace io {

// 文件传输任务
struct FileTransferTask {
    std::string source_path;
    std::string dest_path;
    size_t file_size;
    bool is_directory;
    
    FileTransferTask(const std::string& src, const std::string& dst, size_t size, bool dir)
        : source_path(src), dest_path(dst), file_size(size), is_directory(dir) {}
};

// 传输统计
struct TransferStats {
    std::atomic<uint64_t> total_files{0};
    std::atomic<uint64_t> total_bytes{0};
    std::atomic<uint64_t> transferred_bytes{0};
    std::atomic<uint64_t> directories{0};
    std::atomic<uint64_t> errors{0};
    std::chrono::steady_clock::time_point start_time;
    
    TransferStats() {
        start_time = std::chrono::steady_clock::now();
    }
    
    double get_speed_mbps() const {
        auto now = std::chrono::steady_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(now - start_time).count();
        if (duration == 0) return 0.0;
        return (transferred_bytes.load() / 1024.0 / 1024.0) / (duration / 1000.0);
    }
    
    double get_progress() const {
        if (total_bytes.load() == 0) return 0.0;
        return (transferred_bytes.load() * 100.0) / total_bytes.load();
    }
};

// 传输配置
struct FolderTransferConfig {
    size_t traversal_thread_count{4};      // BFS遍历线程数
    size_t transfer_thread_count{4};       // 传输线程数
    size_t chunk_size{64 * 1024};         // 64KB 分块大小
    size_t max_queue_size{1000};          // 任务队列最大长度
    size_t concurrency_limit{16};          // Gate并发度限制
    bool use_sendfile{true};               // 大文件使用sendfile
    size_t sendfile_threshold{1 * 1024 * 1024}; // 1MB以上使用sendfile
    size_t small_file_threshold{256 * 1024};    // 256KB以下为小文件
    size_t small_file_batch_size{4};       // 小文件批量数量
    bool verify{false};                    // 传输后验证
};

// 并发控制Gate
class Gate {
public:
    explicit Gate(size_t capacity) : capacity_(capacity), count_(0) {}
    
    void enter() {
        std::unique_lock<std::mutex> lock(mutex_);
        cv_.wait(lock, [this]() { return count_ < capacity_; });
        ++count_;
    }
    
    void exit() {
        std::unique_lock<std::mutex> lock(mutex_);
        --count_;
        cv_.notify_one();
    }
    
    size_t capacity() const { return capacity_; }
    size_t count() const { return count_; }
    
private:
    size_t capacity_;
    std::atomic<size_t> count_;
    std::mutex mutex_;
    std::condition_variable cv_;
};

// 小文件批量传输缓冲区
struct SmallFileBatch {
    static constexpr size_t MAX_FILES = 32;
    static constexpr size_t BUFFER_SIZE = 1 * 1024 * 1024; // 1MB
    
    struct FileEntry {
        std::string path;
        size_t offset;
        size_t size;
    };
    
    std::vector<FileEntry> files;
    std::vector<char> buffer;
    size_t current_offset{0};
    
    SmallFileBatch() {
        files.reserve(MAX_FILES);
        buffer.resize(BUFFER_SIZE);
    }
    
    bool can_add(size_t file_size) const {
        return files.size() < MAX_FILES && 
               (current_offset + file_size) <= BUFFER_SIZE;
    }
    
    void add(const std::string& path, const char* data, size_t size) {
        files.push_back({path, current_offset, size});
        std::memcpy(buffer.data() + current_offset, data, size);
        current_offset += size;
    }
    
    bool empty() const { return files.empty(); }
    void clear() { 
        files.clear(); 
        current_offset = 0; 
    }
};

// 文件夹传输器 - 超高性能BFS并行 + sendfile零拷贝
class FolderTransfer {
public:
    // 进度回调类型
    using ProgressCallback = std::function<void(const TransferStats&)>;
    // 完成回调类型
    using CompletionCallback = std::function<void(bool success, const std::string& message)>;
    
    FolderTransfer(const FolderTransferConfig& config = FolderTransferConfig{});
    ~FolderTransfer();
    
    // 上传文件夹（本地 -> 远程）
    bool upload_folder(const std::string& local_folder, const std::string& /*remote_base_path*/,
                      ProgressCallback progress_cb = nullptr,
                      CompletionCallback completion_cb = nullptr);
    
    // 下载文件夹（远程 -> 本地）
    bool download_folder(const std::string& remote_folder, const std::string& local_base_path,
                        ProgressCallback progress_cb = nullptr,
                        CompletionCallback completion_cb = nullptr);
    
    // 复制文件夹（本地 -> 本地）
    bool copy_folder(const std::string& source, const std::string& destination,
                    ProgressCallback progress_cb = nullptr,
                    CompletionCallback completion_cb = nullptr);
    
    // 获取统计信息
    const TransferStats& get_stats() const { return stats_; }
    
    // 停止传输
    void stop();
    
    // 暂停传输
    void pause();
    
    // 恢复传输
    void resume();
    
private:
    // BFS并行遍历文件夹（多线程并行遍历不同子目录）
    void parallel_bfs_traverse(const std::string& path, const std::string& base_path);
    
    // BFS遍历工作线程
    void bfs_worker_thread();
    
    // 创建目录
    bool create_directory(const std::string& path);
    
    // 传输单个文件（根据大小选择策略）
    bool transfer_file(const FileTransferTask& task);
    
    // 使用sendfile零拷贝传输大文件
    bool transfer_file_sendfile(const std::string& src, const std::string& dst, size_t size);
    
    // 小文件批量传输
    bool transfer_small_file_batch(const FileTransferTask& task);
    
    // 刷新小文件批量缓冲区
    void flush_small_file_batch();
    
    // 使用mmap传输大文件
    bool transfer_file_mmap(const std::string& src, const std::string& dst, size_t size);
    
    // 使用read/write传输文件
    bool transfer_file_readwrite(const std::string& src, const std::string& dst, size_t size);
    
    // 验证文件
    bool verify_file(const std::string& src, const std::string& dst);
    
    // 传输工作线程
    void transfer_worker_thread();
    
    // 处理任务队列
    void process_tasks();
    
    FolderTransferConfig config_;
    TransferStats stats_;
    
    // BFS遍历队列（多线程共享）
    std::queue<std::string> bfs_queue_;
    std::mutex bfs_mutex_;
    std::condition_variable bfs_cv_;
    std::atomic<bool> bfs_complete_{false};
    
    // 传输任务队列
    std::queue<FileTransferTask> task_queue_;
    std::mutex queue_mutex_;
    std::condition_variable queue_cv_;
    
    // 并发控制Gate
    std::unique_ptr<Gate> transfer_gate_;
    
    // 工作线程
    std::vector<std::thread> bfs_worker_threads_;
    std::vector<std::thread> transfer_worker_threads_;
    std::atomic<bool> running_{false};
    std::atomic<bool> paused_{false};
    
    ProgressCallback progress_callback_;
    CompletionCallback completion_callback_;
    
    // 小文件批量缓冲区
    SmallFileBatch small_file_batch_;
    std::mutex batch_mutex_;
    
    // 内存池（避免频繁分配）
    struct BufferPool {
        static constexpr size_t POOL_SIZE = 32;
        static constexpr size_t BUFFER_SIZE = 64 * 1024; // 64KB
        
        std::vector<void*> buffers;
        std::mutex mutex;
        
        BufferPool() {
            buffers.reserve(POOL_SIZE);
        }
        
        void* acquire() {
            std::lock_guard<std::mutex> lock(mutex);
            if (buffers.empty()) {
                void* ptr = nullptr;
                posix_memalign(&ptr, 4096, BUFFER_SIZE);
                return ptr;
            }
            void* buf = buffers.back();
            buffers.pop_back();
            return buf;
        }
        
        void release(void* buffer) {
            std::lock_guard<std::mutex> lock(mutex);
            if (buffers.size() < POOL_SIZE) {
                buffers.push_back(buffer);
            } else {
                ::free(buffer);
            }
        }
        
        ~BufferPool() {
            for (void* buf : buffers) {
                ::free(buf);
            }
        }
    };
    
    BufferPool buffer_pool_;
};

// 高性能的文件夹压缩/解压
class FolderArchiver {
public:
    // 定义与 FolderTransfer 相同的回调类型
    using ProgressCallback = std::function<void(const TransferStats&)>;
    
    static bool compress_folder(const std::string& folder, const std::string& output_zip,
                               ProgressCallback progress = nullptr);
    
    static bool decompress_to_folder(const std::string& input_zip, const std::string& output_folder,
                                    ProgressCallback progress = nullptr);
    
    // 获取文件夹大小（BFS遍历）
    static size_t get_folder_size(const std::string& path);
    
    // 获取文件夹统计
    struct FolderInfo {
        size_t total_files;
        size_t total_directories;
        size_t total_bytes;
        std::vector<std::string> file_list;
    };
    
    static FolderInfo get_folder_info(const std::string& path);
};

} // namespace io
} // namespace best_server

#endif // BEST_SERVER_IO_FOLDER_TRANSFER_HPP