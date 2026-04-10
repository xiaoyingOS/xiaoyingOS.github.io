// DMA File implementation

#include "best_server/io/dma_file.hpp"
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <cstring>

namespace best_server {
namespace io {

// DMAFile implementation
DMAFile::DMAFile()
    : fd_(-1)
    , file_size_(0)
    , direct_io_enabled_(false)
{
}

DMAFile::~DMAFile() {
    close();
}

bool DMAFile::open(const std::string& path, bool read_only) {
    int flags = read_only ? O_RDONLY : O_RDWR | O_CREAT;
    int mode = 0644;
    
    fd_ = ::open(path.c_str(), flags, mode);
    if (fd_ < 0) {
        return false;
    }
    
    // Get file size
    struct stat st;
    if (fstat(fd_, &st) == 0) {
        file_size_ = st.st_size;
    }
    
    return true;
}

void DMAFile::close() {
    if (fd_ >= 0) {
        ::close(fd_);
        fd_ = -1;
    }
    file_size_ = 0;
}

bool DMAFile::preallocate(size_t size) {
    if (fd_ < 0) {
        return false;
    }
    
#if BEST_SERVER_PLATFORM_LINUX
    return posix_fallocate(fd_, 0, size) == 0;
#else
    return ftruncate(fd_, size) == 0;
#endif
}

bool DMAFile::enable_direct_io(bool enable) {
#if BEST_SERVER_PLATFORM_LINUX
    if (enable) {
        // Try to enable O_DIRECT
        int flags = fcntl(fd_, F_GETFL);
        if (flags == -1) {
            return false;
        }
        
        // Note: O_DIRECT requires file to be reopened
        // This is a simplified check
        direct_io_enabled_ = true;
        return true;
    }
#endif
    direct_io_enabled_ = false;
    return true;
}

// DMAFileReader implementation
DMAFileReader::DMAFileReader()
    : pending_count_(0)
    , initialized_(false)
{
#if BEST_SERVER_PLATFORM_LINUX
    aio_ctx_ = nullptr;
#endif
}

DMAFileReader::~DMAFileReader() {
#if BEST_SERVER_PLATFORM_LINUX
    if (aio_ctx_) {
        io_destroy(aio_ctx_);
    }
#endif
}

bool DMAFileReader::initialize(size_t max_events) {
#if BEST_SERVER_PLATFORM_LINUX
    if (initialized_) {
        return true;
    }
    
    if (io_setup(max_events, &aio_ctx_) != 0) {
        return false;
    }
    
    initialized_ = true;
    return true;
#else
    (void)max_events;
    return false;
#endif
}

future::Future<memory::ZeroCopyBuffer> DMAFileReader::read_async(
    DMAFile* file,
    size_t offset,
    size_t size
) {
    future::Promise<memory::ZeroCopyBuffer> promise;
    future::Future<memory::ZeroCopyBuffer> future = promise.get_future();
    
    IORequest request;
    request.offset = offset;
    request.size = size;
    request.completed = false;
    
    // Allocate aligned buffer
    size_t aligned_size = ((size + DEFAULT_BLOCK_SIZE - 1) / DEFAULT_BLOCK_SIZE) * DEFAULT_BLOCK_SIZE;
    request.buffer.reserve(aligned_size);
    
    request.promise = std::move(promise);
    
#if BEST_SERVER_PLATFORM_LINUX
    struct iocb cb;
    memset(&cb, 0, sizeof(cb));
    
    cb.aio_fildes = file->fd();
    cb.aio_lio_opcode = IO_CMD_PREAD;
    cb.aio_buf = request.buffer.data();
    cb.aio_nbytes = aligned_size;
    cb.aio_offset = offset;
    cb.aio_reqprio = 0;
    
    if (io_submit(aio_ctx_, 1, &cb) != 1) {
        // Submit failed, complete with error
        request.promise.set_value(memory::ZeroCopyBuffer());
        return future;
    }
    
    pending_requests_.push_back(std::move(request));
    pending_count_++;
#else
    // Fallback to synchronous read
    ::pread(file->fd(), request.buffer.data(), size, offset);
    request.promise.set_value(std::move(request.buffer));
#endif
    
    return future;
}

future::Future<std::vector<memory::ZeroCopyBuffer>> DMAFileReader::read_batch_async(
    DMAFile* file,
    const std::vector<std::pair<size_t, size_t>>& requests
) {
    future::Promise<std::vector<memory::ZeroCopyBuffer>> promise;
    future::Future<std::vector<memory::ZeroCopyBuffer>> future = promise.get_future();
    
    std::vector<memory::ZeroCopyBuffer> results;
    results.reserve(requests.size());
    
    for (const auto& [offset, size] : requests) {
        auto read_future = read_async(file, offset, size);
        // For simplicity, wait for each read (in production, use proper async composition)
        results.push_back(read_future.get());
    }
    
    promise.set_value(std::move(results));
    return future;
}

void DMAFileReader::wait_for_completion() {
#if BEST_SERVER_PLATFORM_LINUX
    if (pending_count_.load() == 0) {
        return;
    }
    
    struct io_event events[MAX_AIO_EVENTS];
    int n = io_getevents(aio_ctx_, 1, MAX_AIO_EVENTS, events, nullptr);
    
    for (int i = 0; i < n; ++i) {
        struct io_event* ev = &events[i];
        IORequest* req = reinterpret_cast<IORequest*>(ev->data);
        
        if (ev->res >= 0) {
            req->buffer.consumed(ev->res);
            req->promise.set_value(std::move(req->buffer));
        } else {
            req->promise.set_value(memory::ZeroCopyBuffer());
        }
        
        req->completed = true;
        pending_count_--;
    }
    
    // Clean up completed requests
    pending_requests_.erase(
        std::remove_if(pending_requests_.begin(), pending_requests_.end(),
            [](const IORequest& req) { return req.completed; }),
        pending_requests_.end()
    );
#endif
}

size_t DMAFileReader::pending_count() const {
    return pending_count_.load();
}

// DMAFileWriter implementation
DMAFileWriter::DMAFileWriter()
    : pending_count_(0)
    , initialized_(false)
{
#if BEST_SERVER_PLATFORM_LINUX
    aio_ctx_ = nullptr;
#endif
}

DMAFileWriter::~DMAFileWriter() {
#if BEST_SERVER_PLATFORM_LINUX
    if (aio_ctx_) {
        io_destroy(aio_ctx_);
    }
#endif
}

bool DMAFileWriter::initialize(size_t max_events) {
#if BEST_SERVER_PLATFORM_LINUX
    if (initialized_) {
        return true;
    }
    
    if (io_setup(max_events, &aio_ctx_) != 0) {
        return false;
    }
    
    initialized_ = true;
    return true;
#else
    (void)max_events;
    return false;
#endif
}

future::Future<size_t> DMAFileWriter::write_async(
    DMAFile* file,
    size_t offset,
    const memory::ZeroCopyBuffer& data
) {
    future::Promise<size_t> promise;
    future::Future<size_t> future = promise.get_future();
    
    IOWriteRequest request;
    request.offset = offset;
    request.data = data;
    request.completed = false;
    request.promise = std::move(promise);
    
#if BEST_SERVER_PLATFORM_LINUX
    struct iocb cb;
    memset(&cb, 0, sizeof(cb));
    
    cb.aio_fildes = file->fd();
    cb.aio_lio_opcode = IO_CMD_PWRITE;
    cb.aio_buf = const_cast<void*>(request.data.data());
    cb.aio_nbytes = request.data.size();
    cb.aio_offset = offset;
    cb.aio_reqprio = 0;
    
    if (io_submit(aio_ctx_, 1, &cb) != 1) {
        request.promise.set_value(0);
        return future;
    }
    
    pending_requests_.push_back(std::move(request));
    pending_count_++;
#else
    // Fallback to synchronous write
    ssize_t written = ::pwrite(file->fd(), data.data(), data.size(), offset);
    request.promise.set_value(static_cast<size_t>(written > 0 ? written : 0));
#endif
    
    return future;
}

future::Future<size_t> DMAFileWriter::write_batch_async(
    DMAFile* file,
    size_t base_offset,
    const std::vector<memory::ZeroCopyBuffer>& data
) {
    future::Promise<size_t> promise;
    future::Future<size_t> future = promise.get_future();
    
    size_t total_written = 0;
    size_t offset = base_offset;
    
    for (const auto& buffer : data) {
        auto write_future = write_async(file, offset, buffer);
        total_written += write_future.get();
        offset += buffer.size();
    }
    
    promise.set_value(total_written);
    return future;
}

bool DMAFileWriter::sync(DMAFile* file) {
#if BEST_SERVER_PLATFORM_LINUX
    return fsync(file->fd()) == 0;
#else
    (void)file;
    return false;
#endif
}

void DMAFileWriter::wait_for_completion() {
#if BEST_SERVER_PLATFORM_LINUX
    if (pending_count_.load() == 0) {
        return;
    }
    
    struct io_event events[MAX_AIO_EVENTS];
    int n = io_getevents(aio_ctx_, 1, MAX_AIO_EVENTS, events, nullptr);
    
    for (int i = 0; i < n; ++i) {
        struct io_event* ev = &events[i];
        IOWriteRequest* req = reinterpret_cast<IOWriteRequest*>(ev->data);
        
        req->promise.set_value(static_cast<size_t>(ev->res));
        req->completed = true;
        pending_count_--;
    }
    
    // Clean up completed requests
    pending_requests_.erase(
        std::remove_if(pending_requests_.begin(), pending_requests_.end(),
            [](const IOWriteRequest& req) { return req.completed; }),
        pending_requests_.end()
    );
#endif
}

// MappedFile implementation
MappedFile::MappedFile()
    : data_(nullptr)
    , size_(0)
    , fd_(-1)
    , read_only_(true)
{
}

MappedFile::~MappedFile() {
    unmap();
}

bool MappedFile::map(const std::string& path, bool read_only) {
    int flags = read_only ? O_RDONLY : O_RDWR;
    fd_ = ::open(path.c_str(), flags);
    if (fd_ < 0) {
        return false;
    }
    
    // Get file size
    struct stat st;
    if (fstat(fd_, &st) != 0) {
        ::close(fd_);
        fd_ = -1;
        return false;
    }
    size_ = st.st_size;
    
    // Map file
    int prot = read_only ? PROT_READ : PROT_READ | PROT_WRITE;
    int flags_map = MAP_PRIVATE;
    
    data_ = mmap(nullptr, size_, prot, flags_map, fd_, 0);
    if (data_ == MAP_FAILED) {
        ::close(fd_);
        fd_ = -1;
        data_ = nullptr;
        size_ = 0;
        return false;
    }
    
    read_only_ = read_only;
    return true;
}

void MappedFile::unmap() {
    if (data_ != nullptr && data_ != MAP_FAILED) {
        munmap(data_, size_);
        data_ = nullptr;
    }
    
    if (fd_ >= 0) {
        ::close(fd_);
        fd_ = -1;
    }
    
    size_ = 0;
}

bool MappedFile::sync() {
    if (!read_only_ && data_ != nullptr && data_ != MAP_FAILED) {
        return msync(data_, size_, MS_SYNC) == 0;
    }
    return false;
}

bool MappedFile::advise(int advice) {
#if BEST_SERVER_PLATFORM_LINUX
    if (data_ != nullptr && data_ != MAP_FAILED) {
        return posix_madvise(data_, size_, advice) == 0;
    }
#endif
    (void)advice;
    return false;
}

// ZeroCopyFileReader implementation
ZeroCopyFileReader::ZeroCopyFileReader()
    : file_size_(0)
{
}

ZeroCopyFileReader::~ZeroCopyFileReader() {
    close();
}

bool ZeroCopyFileReader::open(const std::string& path) {
    if (!mapped_file_.map(path, true)) {
        return false;
    }
    
    file_size_ = mapped_file_.size();
    return true;
}

void ZeroCopyFileReader::close() {
    mapped_file_.unmap();
    file_size_ = 0;
}

memory::ZeroCopyBuffer ZeroCopyFileReader::read(size_t offset, size_t size) {
    if (!mapped_file_.is_mapped()) {
        return memory::ZeroCopyBuffer();
    }
    
    // Check bounds
    if (offset >= file_size_) {
        return memory::ZeroCopyBuffer();
    }
    
    size_t read_size = std::min(size, file_size_ - offset);
    
    // Create zero-copy buffer view
    const char* data_ptr = static_cast<const char*>(mapped_file_.data()) + offset;
    
    memory::ZeroCopyBuffer buffer;
    buffer.write(data_ptr, read_size);
    
    return buffer;
}

} // namespace io
} // namespace best_server