// Logger implementation
#include "best_server/logger/logger.hpp"
#include <iostream>
#include <iomanip>
#include <ctime>
#include <algorithm>

namespace best_server {
namespace logger {

// Entry implementation
std::string Entry::to_string() const {
    std::ostringstream oss;
    
    // Timestamp
    auto time_t = std::chrono::system_clock::to_time_t(timestamp);
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        timestamp.time_since_epoch()) % 1000;
    
    oss << std::put_time(std::localtime(&time_t), "%Y-%m-%d %H:%M:%S");
    oss << '.' << std::setfill('0') << std::setw(3) << ms.count();
    
    // Level
    oss << " [";
    switch (level) {
        case Level::Trace: oss << "TRACE"; break;
        case Level::Debug: oss << "DEBUG"; break;
        case Level::Info:  oss << "INFO "; break;
        case Level::Warning: oss << "WARN "; break;
        case Level::Error: oss << "ERROR"; break;
        case Level::Fatal: oss << "FATAL"; break;
    }
    oss << "] ";
    
    // Thread ID
    oss << "[" << thread_id << "] ";
    
    // Message
    oss << message;
    
    return oss.str();
}

// ConsoleAppender implementation
ConsoleAppender::Ptr ConsoleAppender::create(bool use_color) {
    return std::make_shared<ConsoleAppender>(use_color);
}

ConsoleAppender::ConsoleAppender(bool use_color)
    : use_color_(use_color)
{
}

void ConsoleAppender::append(const Entry& entry) {
    std::lock_guard<std::mutex> lock(const_cast<std::mutex&>(mutex_));
    
    std::ostream& stream = (entry.level >= Level::Error) ? std::cerr : std::cout;
    
    if (use_color_) {
        const char* color = "\033[0m";  // Reset
        
        switch (entry.level) {
            case Level::Trace:   color = "\033[90m"; break;  // Gray
            case Level::Debug:   color = "\033[36m"; break;  // Cyan
            case Level::Info:    color = "\033[32m"; break;  // Green
            case Level::Warning: color = "\033[33m"; break;  // Yellow
            case Level::Error:   color = "\033[31m"; break;  // Red
            case Level::Fatal:   color = "\033[35m"; break;  // Magenta
        }
        
        stream << color << entry.to_string() << "\033[0m" << std::endl;
    } else {
        stream << entry.to_string() << std::endl;
    }
}

void ConsoleAppender::flush() {
    std::cout.flush();
    std::cerr.flush();
}

// FileAppender implementation
FileAppender::Ptr FileAppender::create(const std::string& file_path, bool append) {
    return std::make_shared<FileAppender>(file_path, append);
}

FileAppender::FileAppender(const std::string& file_path, bool append)
    : file_path_(file_path)
{
    file_ = fopen(file_path.c_str(), append ? "a" : "w");
}

FileAppender::~FileAppender() {
    if (file_) {
        fclose(file_);
    }
}

void FileAppender::append(const Entry& entry) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (!file_) {
        return;
    }
    
    std::string message = entry.to_string() + "\n";
    fwrite(message.data(), 1, message.size(), file_);
    current_size_ += message.size();
    
    // Check if rotation is needed
    if (current_size_ >= max_file_size_) {
        rotate_file();
    }
}

void FileAppender::flush() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (file_) {
        fflush(file_);
    }
}

void FileAppender::set_max_file_size(size_t size) {
    max_file_size_ = size;
}

void FileAppender::set_max_files(int count) {
    max_files_ = count;
}

void FileAppender::rotate_file() {
    if (!file_) {
        return;
    }
    
    fclose(file_);
    file_ = nullptr;
    
    // Rotate existing files
    for (int i = max_files_ - 1; i > 0; i--) {
        std::string old_name = file_path_ + "." + std::to_string(i);
        std::string new_name = file_path_ + "." + std::to_string(i + 1);
        
        std::remove(new_name.c_str());
        std::rename(old_name.c_str(), new_name.c_str());
    }
    
    // Move current file to .1
    std::string backup_name = file_path_ + ".1";
    std::rename(file_path_.c_str(), backup_name.c_str());
    
    // Open new file
    file_ = fopen(file_path_.c_str(), "w");
    current_size_ = 0;
}

// AsyncAppender implementation
AsyncAppender::Ptr AsyncAppender::create(Appender::Ptr target, size_t queue_size) {
    return std::make_shared<AsyncAppender>(target, queue_size);
}

AsyncAppender::AsyncAppender(Appender::Ptr target, size_t queue_size)
    : target_(target)
{
    (void)queue_size; // LockFreeRingBuffer has fixed size
    thread_ = std::thread(&AsyncAppender::run, this);
}

AsyncAppender::~AsyncAppender() {
    running_.store(false, std::memory_order_release);
    
    if (thread_.joinable()) {
        thread_.join();
    }
    
    // Flush remaining entries
    flush();
}

void AsyncAppender::append(const Entry& entry) {
    // Lock-free push to ring buffer
    if (!ring_buffer_.push(entry)) {
        // Buffer is full, drop entry (or could block)
        // In production, you might want to log this event
    }
}

void AsyncAppender::flush() {
    // Drain the ring buffer
    Entry entry;
    while (ring_buffer_.pop(entry)) {
        target_->append(entry);
    }
    target_->flush();
}

void AsyncAppender::run() {
    Entry entry;
    
    while (running_.load(std::memory_order_acquire)) {
        if (ring_buffer_.pop(entry)) {
            target_->append(entry);
        } else {
            // Buffer empty, sleep briefly
            std::this_thread::sleep_for(std::chrono::microseconds(100));
        }
    }
    
    // Flush remaining entries
    flush();
}

// PatternFormatter implementation
Formatter::Ptr Formatter::create_pattern(const std::string& pattern) {
    return std::make_shared<PatternFormatter>(pattern);
}

PatternFormatter::PatternFormatter(const std::string& pattern)
    : pattern_(pattern)
{
}

std::string PatternFormatter::format(const Entry& entry) {
    std::string result = pattern_;
    
    // Replace placeholders
    result = replace_all(result, "%d", format_timestamp(entry.timestamp));
    result = replace_all(result, "%l", level_to_string(entry.level));
    result = replace_all(result, "%m", entry.message);
    result = replace_all(result, "%f", entry.file);
    result = replace_all(result, "%n", std::to_string(entry.line));
    result = replace_all(result, "%F", entry.function);
    
    return result;
}

std::string PatternFormatter::replace_all(std::string str, const std::string& from, const std::string& to) {
    size_t pos = 0;
    while ((pos = str.find(from, pos)) != std::string::npos) {
        str.replace(pos, from.length(), to);
        pos += to.length();
    }
    return str;
}

std::string PatternFormatter::format_timestamp(const std::chrono::system_clock::time_point& tp) {
    auto time_t = std::chrono::system_clock::to_time_t(tp);
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        tp.time_since_epoch()) % 1000;
    
    std::ostringstream oss;
    oss << std::put_time(std::localtime(&time_t), "%Y-%m-%d %H:%M:%S");
    oss << '.' << std::setfill('0') << std::setw(3) << ms.count();
    return oss.str();
}

std::string PatternFormatter::level_to_string(Level level) {
    switch (level) {
        case Level::Trace:   return "TRACE";
        case Level::Debug:   return "DEBUG";
        case Level::Info:    return "INFO";
        case Level::Warning: return "WARNING";
        case Level::Error:   return "ERROR";
        case Level::Fatal:   return "FATAL";
        default:             return "UNKNOWN";
    }
}

// Logger implementation
Logger::Ptr Logger::get(const std::string& name) {
    static std::unordered_map<std::string, Logger::Ptr> loggers;
    static std::mutex mutex;
    
    std::lock_guard<std::mutex> lock(mutex);
    
    auto it = loggers.find(name);
    if (it != loggers.end()) {
        return it->second;
    }
    
    auto logger = Logger::create(name);
    loggers[name] = logger;
    
    // Configure root logger with default appenders
    if (name == "root" && logger->appenders_.empty()) {
        logger->add_appender(ConsoleAppender::create());
    }
    
    return logger;
}

Logger::Ptr Logger::create(const std::string& name) {
    return std::make_shared<Logger>(name);
}

Logger::Logger(const std::string& name)
    : name_(name)
    , formatter_(Formatter::create_pattern("%d [%l] %m"))
{
}

void Logger::set_level(Level level) {
    std::lock_guard<std::shared_mutex> lock(mutex_);
    level_ = level;
}

void Logger::add_appender(Appender::Ptr appender) {
    std::lock_guard<std::shared_mutex> lock(mutex_);
    appenders_.push_back(appender);
}

void Logger::remove_appender(Appender::Ptr appender) {
    std::lock_guard<std::shared_mutex> lock(mutex_);
    appenders_.erase(
        std::remove(appenders_.begin(), appenders_.end(), appender),
        appenders_.end());
}

void Logger::clear_appenders() {
    std::lock_guard<std::shared_mutex> lock(mutex_);
    appenders_.clear();
}

void Logger::log(Level level, const std::string& message,
                 const char* file, int line, const char* function) {
    if (level < level_) {
        return;
    }
    
    Entry entry;
    entry.level = level;
    entry.message = message;
    entry.file = file;
    entry.line = line;
    entry.function = function;
    entry.timestamp = std::chrono::system_clock::now();
    entry.thread_id = std::this_thread::get_id();
    
    std::shared_lock<std::shared_mutex> lock(mutex_);
    
    for (auto& appender : appenders_) {
        appender->append(entry);
    }
}

// Global logger configuration
void set_default_level(Level level) {
    Logger::get("root")->set_level(level);
}

void add_default_appender(Appender::Ptr appender) {
    Logger::get("root")->add_appender(appender);
}

void set_default_formatter(Formatter::Ptr formatter) {
    // This would need to be implemented in Logger class
}

} // namespace logger
} // namespace best_server