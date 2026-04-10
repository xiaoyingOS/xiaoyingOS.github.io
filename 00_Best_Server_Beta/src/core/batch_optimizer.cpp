// Batch Optimizer implementation

#include "best_server/core/batch_optimizer.hpp"
#include <algorithm>

namespace best_server {
namespace core {

// BatchOptimizer implementation
BatchOptimizer::BatchOptimizer(const BatchConfig& config)
    : config_(config)
    , last_flush_(std::chrono::steady_clock::now())
{
}

BatchOptimizer::~BatchOptimizer() {
    flush();
}

void BatchOptimizer::add_operation(BatchOperation&& op) {
    op.enqueue_time = std::chrono::steady_clock::now();
    pending_ops_.push_back(std::move(op));
    
    total_operations_++;
    
    check_and_flush();
}

void BatchOptimizer::set_callback(BatchCallback callback) {
    callback_ = std::move(callback);
}

void BatchOptimizer::flush() {
    if (pending_ops_.empty() || !callback_) {
        return;
    }
    
    flushing_.store(true);
    
    // Move operations to execute
    std::vector<BatchOperation> batch;
    batch.reserve(pending_ops_.size());
    
    while (!pending_ops_.empty()) {
        batch.push_back(std::move(pending_ops_.front()));
        pending_ops_.pop_front();
    }
    
    // Execute batch
    callback_(batch);
    
    // Update statistics
    batches_executed_++;
    operations_batched_ += batch.size();
    
    last_flush_ = std::chrono::steady_clock::now();
    flushing_.store(false);
}

BatchOptimizer::Statistics BatchOptimizer::get_statistics() const {
    Statistics stats;
    stats.total_operations = total_operations_.load();
    stats.batches_executed = batches_executed_.load();
    stats.operations_batched = operations_batched_.load();
    
    if (stats.batches_executed > 0) {
        stats.batch_efficiency = static_cast<double>(stats.operations_batched) / 
                                stats.batches_executed;
    } else {
        stats.batch_efficiency = 0.0;
    }
    
    // Calculate average batch delay (simplified)
    stats.avg_batch_delay = config_.max_batch_delay;
    
    return stats;
}

void BatchOptimizer::update_config(const BatchConfig& config) {
    config_ = config;
}

void BatchOptimizer::check_and_flush() {
    if (should_flush()) {
        flush();
    }
}

bool BatchOptimizer::should_flush() const {
    if (pending_ops_.empty()) {
        return false;
    }
    
    // Check size-based batching
    if (config_.enable_size_based_batch && 
        pending_ops_.size() >= config_.max_batch_size) {
        return true;
    }
    
    // Check deadline-based batching
    if (config_.enable_deadline_batch) {
        auto now = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(
            now - last_flush_
        );
        
        if (elapsed >= config_.max_batch_delay) {
            return true;
        }
    }
    
    return false;
}

// IOBatchHelper implementation
IOBatchHelper::IOBatchHelper(BatchOptimizer* optimizer)
    : optimizer_(optimizer)
{
}

IOBatchHelper::~IOBatchHelper() {
    execute();
}

void IOBatchHelper::add_write(void* target, const memory::ZeroCopyBuffer& data) {
    BatchOperation op;
    op.type = BatchOperation::Type::Write;
    op.target = target;
    op.data = data;
    
    operations_.push_back(op);
}

void IOBatchHelper::add_read(void* target, size_t offset, size_t size) {
    BatchOperation op;
    op.type = BatchOperation::Type::Read;
    op.target = target;
    op.offset = offset;
    op.data.reserve(size);
    
    operations_.push_back(op);
}

void IOBatchHelper::execute() {
    if (!operations_.empty() && optimizer_) {
        for (auto& op : operations_) {
            optimizer_->add_operation(std::move(op));
        }
        operations_.clear();
    }
}

// ScatterGatherBatch implementation
ScatterGatherBatch::ScatterGatherBatch()
    : write_count_(0)
    , read_count_(0)
{
    memset(write_iov_, 0, sizeof(write_iov_));
    memset(read_iov_, 0, sizeof(read_iov_));
}

ScatterGatherBatch::~ScatterGatherBatch() {
    clear();
}

void ScatterGatherBatch::add_write_buffer(const memory::ZeroCopyBuffer& buffer) {
    if (write_count_ < MAX_IOV && buffer.size() > 0) {
        write_iov_[write_count_].iov_base = const_cast<char*>(buffer.data());
        write_iov_[write_count_].iov_len = buffer.size();
        write_count_++;
        
        // Keep reference to buffer
        write_buffers_.push_back(buffer);
    }
}

void ScatterGatherBatch::add_read_buffer(void* buffer, size_t size) {
    if (read_count_ < MAX_IOV && size > 0) {
        read_iov_[read_count_].iov_base = buffer;
        read_iov_[read_count_].iov_len = size;
        read_count_++;
        
        // Keep reference to buffer
        read_buffers_.push_back(buffer);
    }
}

size_t ScatterGatherBatch::execute_scatter_write(int fd) {
#if BEST_SERVER_PLATFORM_LINUX
    struct mmsghdr msgvec[MAX_IOV];
    
    for (size_t i = 0; i < write_count_; ++i) {
        msgvec[i].msg_hdr.msg_iov = &write_iov_[i];
        msgvec[i].msg_hdr.msg_iovlen = 1;
        msgvec[i].msg_len = 0;
    }
    
    ssize_t sent = sendmmsg(fd, msgvec, write_count_, MSG_DONTWAIT | MSG_NOSIGNAL);
    return (sent > 0) ? static_cast<size_t>(sent) : 0;
#else
    (void)fd;
    return 0;
#endif
}

size_t ScatterGatherBatch::execute_gather_read(int fd) {
#if BEST_SERVER_PLATFORM_LINUX
    struct mmsghdr msgvec[MAX_IOV];
    
    for (size_t i = 0; i < read_count_; ++i) {
        msgvec[i].msg_hdr.msg_iov = &read_iov_[i];
        msgvec[i].msg_hdr.msg_iovlen = 1;
        msgvec[i].msg_len = 0;
    }
    
    ssize_t received = recvmmsg(fd, msgvec, read_count_, MSG_DONTWAIT | MSG_NOSIGNAL, nullptr);
    return (received > 0) ? static_cast<size_t>(received) : 0;
#else
    (void)fd;
    return 0;
#endif
}

void ScatterGatherBatch::clear() {
    write_count_ = 0;
    read_count_ = 0;
    write_buffers_.clear();
    read_buffers_.clear();
    
    memset(write_iov_, 0, sizeof(write_iov_));
    memset(read_iov_, 0, sizeof(read_iov_));
}

size_t ScatterGatherBatch::total_write_size() const {
    size_t total = 0;
    for (size_t i = 0; i < write_count_; ++i) {
        total += write_iov_[i].iov_len;
    }
    return total;
}

size_t ScatterGatherBatch::total_read_size() const {
    size_t total = 0;
    for (size_t i = 0; i < read_count_; ++i) {
        total += read_iov_[i].iov_len;
    }
    return total;
}

} // namespace core
} // namespace best_server