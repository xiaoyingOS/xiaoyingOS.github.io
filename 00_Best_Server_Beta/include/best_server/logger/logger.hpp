#pragma once

#include <best_server/future/future.hpp>
#include <string>
#include <vector>
#include <memory>
#include <functional>
#include <sstream>
#include <chrono>
#include <thread>
#include <mutex>
#include <atomic>

namespace best_server {
namespace logger {

// Log levels
enum class Level {
    Trace = 0,
    Debug = 1,
    Info = 2,
    Warning = 3,
    Error = 4,
    Fatal = 5
};

// Log entry
struct Entry {
    Level level;
    std::string message;
    std::string file;
    int line;
    std::string function;
    std::chrono::system_clock::time_point timestamp;
    std::thread::id thread_id;
    
    std::string to_string() const;
};

// Log appender interface
class Appender {
public:
    using Ptr = std::shared_ptr<Appender>;
    
    virtual ~Appender() = default;
    virtual void append(const Entry& entry) = 0;
    virtual void flush() = 0;
};

// Console appender
class ConsoleAppender : public Appender {
public:
    static Ptr create(bool use_color = true);
    
    void append(const Entry& entry) override;
    void flush() override;
    
private:
    ConsoleAppender(bool use_color);
    
    bool use_color_;
    Level min_level_{Level::Trace};
};

// File appender
class FileAppender : public Appender {
public:
    static Ptr create(const std::string& file_path, bool append = true);
    
    void append(const Entry& entry) override;
    void flush() override;
    
    void set_max_file_size(size_t size);
    void set_max_files(int count);
    
private:
    FileAppender(const std::string& file_path, bool append);
    
    void rotate_file();
    
    std::string file_path_;
    FILE* file_{nullptr};
    size_t max_file_size_{100 * 1024 * 1024};  // 100MB
    int max_files_{10};
    size_t current_size_{0};
    std::mutex mutex_;
};

// Lock-free ring buffer for high-performance logging
class LockFreeRingBuffer {
public:
    static constexpr size_t BUFFER_SIZE = 65536; // 64K entries
    
    LockFreeRingBuffer() : write_pos_(0), read_pos_(0) {}
    
    bool push(const Entry& entry) {
        uint64_t pos = write_pos_.load(std::memory_order_relaxed);
        uint64_t next_pos = (pos + 1) % BUFFER_SIZE;
        
        // Check if buffer is full
        if (next_pos == read_pos_.load(std::memory_order_acquire)) {
            return false; // Buffer full
        }
        
        // Write entry
        buffer_[pos] = entry;
        
        // Update write position with release semantics
        write_pos_.store(next_pos, std::memory_order_release);
        
        return true;
    }
    
    bool pop(Entry& entry) {
        uint64_t pos = read_pos_.load(std::memory_order_relaxed);
        
        // Check if buffer is empty
        if (pos == write_pos_.load(std::memory_order_acquire)) {
            return false; // Buffer empty
        }
        
        // Read entry
        entry = buffer_[pos];
        
        // Update read position with release semantics
        read_pos_.store((pos + 1) % BUFFER_SIZE, std::memory_order_release);
        
        return true;
    }
    
    size_t size() const {
        uint64_t write = write_pos_.load(std::memory_order_acquire);
        uint64_t read = read_pos_.load(std::memory_order_acquire);
        
        if (write >= read) {
            return write - read;
        } else {
            return BUFFER_SIZE - read + write;
        }
    }
    
    bool empty() const {
        return read_pos_.load(std::memory_order_acquire) == 
               write_pos_.load(std::memory_order_acquire);
    }
    
    bool full() const {
        uint64_t next = (write_pos_.load(std::memory_order_acquire) + 1) % BUFFER_SIZE;
        return next == read_pos_.load(std::memory_order_acquire);
    }
    
private:
    alignas(64) std::array<Entry, BUFFER_SIZE> buffer_;
    alignas(64) std::atomic<uint64_t> write_pos_;
    alignas(64) std::atomic<uint64_t> read_pos_;
};

// Async appender (lock-free version)
class AsyncAppender : public Appender {
public:
    static Ptr create(Appender::Ptr target, size_t queue_size = 10000);

    void append(const Entry& entry) override;
    void flush() override;

    ~AsyncAppender();

private:
    AsyncAppender(Appender::Ptr target, size_t queue_size);

    void run();

    Appender::Ptr target_;
    LockFreeRingBuffer ring_buffer_;
    std::thread thread_;
    std::atomic<bool> running_{true};
};

// Log formatter
class Formatter {
public:
    using Ptr = std::shared_ptr<Formatter>;

    static Ptr create_pattern(const std::string& pattern);

    virtual std::string format(const logger::Entry& entry) = 0;

protected:
    Formatter() = default;
};

// Forward declaration for Entry (defined above)
// Entry is already defined before Formatter in this file

// Pattern formatter
class PatternFormatter : public Formatter {
public:
    explicit PatternFormatter(const std::string& pattern);

    std::string format(const logger::Entry& entry) override;

private:
    std::string pattern_;
};

// Logger
class Logger {
public:
    using Ptr = std::shared_ptr<Logger>;
    
    static Ptr get(const std::string& name = "root");
    static Ptr create(const std::string& name);
    
    const std::string& name() const { return name_; }
    
    void set_level(logger::Level level);
    logger::Level level() const { return level_; }
    
    void add_appender(logger::Appender::Ptr appender);
    void remove_appender(logger::Appender::Ptr appender);
    void clear_appenders();
    
    void log(logger::Level level, const std::string& message,
             const char* file, int line, const char* function);
    
    template<typename... Args>
    void trace(const std::string& fmt, Args&&... args) {
        if (level_ <= logger::Level::Trace) {
            log(logger::Level::Trace, format(fmt, args...), __FILE__, __LINE__, __FUNCTION__);
        }
    }
    
    template<typename... Args>
    void debug(const std::string& fmt, Args&&... args) {
        if (level_ <= logger::Level::Debug) {
            log(logger::Level::Debug, format(fmt, args...), __FILE__, __LINE__, __FUNCTION__);
        }
    }
    
    template<typename... Args>
    void info(const std::string& fmt, Args&&... args) {
        if (level_ <= logger::Level::Info) {
            log(logger::Level::Info, format(fmt, args...), __FILE__, __LINE__, __FUNCTION__);
        }
    }
    
    template<typename... Args>
    void warning(const std::string& fmt, Args&&... args) {
        if (level_ <= logger::Level::Warning) {
            log(logger::Level::Warning, format(fmt, args...), __FILE__, __LINE__, __FUNCTION__);
        }
    }
    
    template<typename... Args>
    void error(const std::string& fmt, Args&&... args) {
        if (level_ <= logger::Level::Error) {
            log(logger::Level::Error, format(fmt, args...), __FILE__, __LINE__, __FUNCTION__);
        }
    }
    
    template<typename... Args>
    void fatal(const std::string& fmt, Args&&... args) {
        if (level_ <= logger::Level::Fatal) {
            log(logger::Level::Fatal, format(fmt, args...), __FILE__, __LINE__, __FUNCTION__);
        }
    }
    
private:
    explicit Logger(const std::string& name);
    
    template<typename... Args>
    static std::string format(const std::string& fmt, Args&&... args) {
        std::ostringstream oss;
        format_impl(oss, fmt, 0, std::forward<Args>(args)...);
        return oss.str();
    }
    
    static void format_impl(std::ostringstream& oss, const std::string& fmt, size_t pos) {
        oss << fmt.substr(pos);
    }
    
    template<typename T, typename... Args>
    static void format_impl(std::ostringstream& oss, const std::string& fmt, size_t pos, T&& arg, Args&&... args) {
        size_t next = fmt.find("{}", pos);
        if (next == std::string::npos) {
            oss << fmt.substr(pos);
            return;
        }
        oss << fmt.substr(pos, next - pos);
        oss << arg;
        format_impl(oss, fmt, next + 2, std::forward<Args>(args)...);
    }
    
    std::string name_;
    logger::Level level_{logger::Level::Info};
    std::vector<logger::Appender::Ptr> appenders_;
    logger::Formatter::Ptr formatter_;
    mutable std::mutex mutex_;
};

// Global logger configuration
void set_default_level(logger::Level level);
void add_default_appender(logger::Appender::Ptr appender);
void set_default_formatter(logger::Formatter::Ptr formatter);

// Convenience macros
#define LOG_TRACE(logger, ...) logger->trace(__VA_ARGS__)
#define LOG_DEBUG(logger, ...) logger->debug(__VA_ARGS__)
#define LOG_INFO(logger, ...) logger->info(__VA_ARGS__)
#define LOG_WARNING(logger, ...) logger->warning(__VA_ARGS__)
#define LOG_ERROR(logger, ...) logger->error(__VA_ARGS__)
#define LOG_FATAL(logger, ...) logger->fatal(__VA_ARGS__)

// Global logger macros
#define LOG_TRACE_GLOBAL(...) LOG_TRACE(logger::Logger::get(), __VA_ARGS__)
#define LOG_DEBUG_GLOBAL(...) LOG_DEBUG(logger::Logger::get(), __VA_ARGS__)
#define LOG_INFO_GLOBAL(...) LOG_INFO(logger::Logger::get(), __VA_ARGS__)
#define LOG_WARNING_GLOBAL(...) LOG_WARNING(logger::Logger::get(), __VA_ARGS__)
#define LOG_ERROR_GLOBAL(...) LOG_ERROR(logger::Logger::get(), __VA_ARGS__)
#define LOG_FATAL_GLOBAL(...) LOG_FATAL(logger::Logger::get(), __VA_ARGS__)

} // namespace logger
} // namespace best_server