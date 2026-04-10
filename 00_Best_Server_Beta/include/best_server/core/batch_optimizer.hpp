// Batch Optimizer - Intelligent batching for I/O operations
//
// Implements automatic batching to reduce system calls:
// - Automatic batch detection
// - Deadline-based batching
// - Size-based batching
// - Priority-aware batching
// - Zero-copy batch operations

#ifndef BEST_SERVER_CORE_BATCH_OPTIMIZER_HPP
#define BEST_SERVER_CORE_BATCH_OPTIMIZER_HPP

#include "best_server/memory/zero_copy_buffer.hpp"
#include "best_server/future/future.hpp"
#include <vector>
#include <deque>
#include <chrono>
#include <functional>
#include <atomic>

namespace best_server {
namespace core {

// Batch operation
struct BatchOperation {
    enum class Type {
        Write,
        Read,
        Send,
        Receive
    };
    
    Type type;
    memory::ZeroCopyBuffer data;
    void* target;
    size_t offset;
    
    std::chrono::steady_clock::time_point enqueue_time;
    std::function<void(size_t)> callback;
};

// Batch configuration
struct BatchConfig {
    size_t max_batch_size{128};                  // Max operations per batch
    size_t max_batch_bytes{65536};               // Max bytes per batch
    std::chrono::microseconds max_batch_delay{100};  // Max wait time
    bool enable_auto_batch{true};                // Enable automatic batching
    bool enable_size_based_batch{true};          // Batch based on size
    bool enable_deadline_batch{true};            // Batch based on deadline
};

// Batch optimizer
class BatchOptimizer {
public:
    using BatchCallback = std::function<void(const std::vector<BatchOperation>&)>;
    
    BatchOptimizer(const BatchConfig& config = BatchConfig{});
    ~BatchOptimizer();
    
    // Add operation to batch
    void add_operation(BatchOperation&& op);
    
    // Set callback for batch execution
    void set_callback(BatchCallback callback);
    
    // Force flush pending operations
    void flush();
    
    // Get statistics
    struct Statistics {
        uint64_t total_operations;
        uint64_t batches_executed;
        uint64_t operations_batched;
        double batch_efficiency;  // avg operations per batch
        std::chrono::microseconds avg_batch_delay;
    };
    Statistics get_statistics() const;
    
    // Update configuration
    void update_config(const BatchConfig& config);
    
private:
    void check_and_flush();
    bool should_flush() const;
    void execute_batch();
    
    BatchConfig config_;
    std::deque<BatchOperation> pending_ops_;
    BatchCallback callback_;
    
    // Statistics
    std::atomic<uint64_t> total_operations_{0};
    std::atomic<uint64_t> batches_executed_{0};
    std::atomic<uint64_t> operations_batched_{0};
    
    std::chrono::steady_clock::time_point last_flush_;
    std::atomic<bool> flushing_{false};
};

// I/O batch helper for socket operations
class IOBatchHelper {
public:
    IOBatchHelper(BatchOptimizer* optimizer);
    ~IOBatchHelper();
    
    // Add write operation
    void add_write(void* target, const memory::ZeroCopyBuffer& data);
    
    // Add read operation
    void add_read(void* target, size_t offset, size_t size);
    
    // Execute batch
    void execute();
    
private:
    BatchOptimizer* optimizer_;
    std::vector<BatchOperation> operations_;
};

// Zero-copy scatter/gather I/O batch
class ScatterGatherBatch {
public:
    static constexpr size_t MAX_IOV = 1024;
    
    ScatterGatherBatch();
    ~ScatterGatherBatch();
    
    // Add buffer to write list
    void add_write_buffer(const memory::ZeroCopyBuffer& buffer);
    
    // Add buffer to read list
    void add_read_buffer(void* buffer, size_t size);
    
    // Execute scatter write (sendmsg)
    size_t execute_scatter_write(int fd);
    
    // Execute gather read (recvmsg)
    size_t execute_gather_read(int fd);
    
    // Clear batch
    void clear();
    
    // Get total write size
    size_t total_write_size() const;
    
    // Get total read size
    size_t total_read_size() const;
    
private:
    struct iovec write_iov_[MAX_IOV];
    struct iovec read_iov_[MAX_IOV];
    size_t write_count_;
    size_t read_count_;
    
    // Keep references to buffers
    std::vector<memory::ZeroCopyBuffer> write_buffers_;
    std::vector<void*> read_buffers_;
};

// Automatic batching wrapper for common operations
template<typename T>
class AutoBatch {
public:
    AutoBatch(BatchOptimizer* optimizer) : optimizer_(optimizer) {}
    
    // Add operation
    void add(T&& op) {
        operations_.push_back(std::forward<T>(op));
        check_flush();
    }
    
    // Flush all operations
    void flush() {
        if (!operations_.empty()) {
            execute_batch();
            operations_.clear();
        }
    }
    
private:
    void check_flush() {
        if (operations_.size() >= 64) {  // Threshold
            flush();
        }
    }
    
    void execute_batch();
    
    BatchOptimizer* optimizer_;
    std::deque<T> operations_;
};

} // namespace core
} // namespace best_server

#endif // BEST_SERVER_CORE_BATCH_OPTIMIZER_HPP