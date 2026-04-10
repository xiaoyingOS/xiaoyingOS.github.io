#ifndef SERVER_STATS_HPP
#define SERVER_STATS_HPP

#include <atomic>
#include <chrono>
#include <string>
#include <deque>
#include <mutex>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <sys/sysinfo.h>
#include <unistd.h>

class ServerStats {
public:
    struct LogEntry {
        std::string time;
        std::string message;
        std::string type; // "info", "success", "error"
    };

    ServerStats() 
        : active_connections_(0)
        , total_requests_(0)
        , status_2xx_(0)
        , status_4xx_(0)
        , status_5xx_(0)
        , bytes_sent_(0)
        , bytes_received_(0)
        , start_time_(std::chrono::steady_clock::now()) {
    }

    void add_connection() {
        active_connections_++;
    }

    void remove_connection() {
        if (active_connections_ > 0) {
            active_connections_--;
        }
    }

    void add_request() {
        total_requests_++;
    }

    void add_response(int status_code) {
        if (status_code >= 200 && status_code < 300) {
            status_2xx_++;
        } else if (status_code >= 400 && status_code < 500) {
            status_4xx_++;
        } else if (status_code >= 500) {
            status_5xx_++;
        }
    }

    void add_bytes_sent(size_t bytes) {
        bytes_sent_ += bytes;
    }

    void add_bytes_received(size_t bytes) {
        bytes_received_ += bytes;
    }

    void add_log(const std::string& message, const std::string& type = "info") {
        std::lock_guard<std::mutex> lock(log_mutex_);
        
        auto now = std::chrono::system_clock::now();
        auto time_t = std::chrono::system_clock::to_time_t(now);
        std::stringstream ss;
        ss << std::put_time(std::localtime(&time_t), "%Y-%m-%d %H:%M:%S");
        
        LogEntry entry{ss.str(), message, type};
        logs_.push_back(entry);
        
        // 限制日志数量为 100 条
        if (logs_.size() > 100) {
            logs_.pop_front();
        }
    }

    std::string get_status_json() const {
        auto now = std::chrono::steady_clock::now();
        auto uptime = std::chrono::duration_cast<std::chrono::seconds>(now - start_time_).count();
        
        std::stringstream ss;
        ss << "{";
        ss << "\"active_connections\": " << active_connections_ << ",";
        ss << "\"total_requests\": " << total_requests_ << ",";
        ss << "\"uptime_seconds\": " << uptime << ",";
        ss << "\"status_2xx\": " << status_2xx_ << ",";
        ss << "\"status_4xx\": " << status_4xx_ << ",";
        ss << "\"status_5xx\": " << status_5xx_ << ",";
        ss << "\"bytes_sent\": " << bytes_sent_ << ",";
        ss << "\"bytes_received\": " << bytes_received_;
        ss << "}";
        return ss.str();
    }

    std::string get_metrics_json() const {
        struct sysinfo info;
        sysinfo(&info);
        
        // 内存使用 (MB)
        size_t total_memory = info.totalram * info.mem_unit / (1024 * 1024);
        size_t free_memory = info.freeram * info.mem_unit / (1024 * 1024);
        size_t used_memory = total_memory - free_memory;
        
        // CPU 负载
        double load1 = info.loads[0] / 65536.0;
        double load5 = info.loads[1] / 65536.0;
        double load15 = info.loads[2] / 65536.0;
        
        // 运行时间
        auto now = std::chrono::steady_clock::now();
        auto uptime = std::chrono::duration_cast<std::chrono::seconds>(now - start_time_).count();
        
        std::stringstream ss;
        ss << "{";
        ss << "\"memory\": {";
        ss << "\"total_mb\": " << total_memory << ",";
        ss << "\"used_mb\": " << used_memory << ",";
        ss << "\"free_mb\": " << free_memory << ",";
        ss << "\"usage_percent\": " << (used_memory * 100.0 / total_memory);
        ss << "},";
        ss << "\"cpu\": {";
        ss << "\"load1\": " << load1 << ",";
        ss << "\"load5\": " << load5 << ",";
        ss << "\"load15\": " << load15;
        ss << "},";
        ss << "\"uptime_seconds\": " << uptime;
        ss << "}";
        return ss.str();
    }

    std::string get_logs_json() const {
        std::lock_guard<std::mutex> lock(log_mutex_);
        
        std::stringstream ss;
        ss << "[";
        
        bool first = true;
        for (const auto& log : logs_) {
            if (!first) ss << ",";
            first = false;
            
            ss << "{";
            ss << "\"time\": \"" << log.time << "\",";
            ss << "\"message\": \"" << escape_json(log.message) << "\",";
            ss << "\"type\": \"" << log.type << "\"";
            ss << "}";
        }
        
        ss << "]";
        return ss.str();
    }

    int get_active_connections() const { return active_connections_; }
    uint64_t get_total_requests() const { return total_requests_; }
    uint64_t get_bytes_sent() const { return bytes_sent_; }
    uint64_t get_bytes_received() const { return bytes_received_; }

private:
    std::atomic<int> active_connections_;
    std::atomic<uint64_t> total_requests_;
    std::atomic<uint64_t> status_2xx_;
    std::atomic<uint64_t> status_4xx_;
    std::atomic<uint64_t> status_5xx_;
    std::atomic<uint64_t> bytes_sent_;
    std::atomic<uint64_t> bytes_received_;
    
    std::chrono::steady_clock::time_point start_time_;
    
    mutable std::mutex log_mutex_;
    std::deque<LogEntry> logs_;

    static std::string escape_json(const std::string& s) {
        std::string result;
        for (char c : s) {
            switch (c) {
                case '"': result += "\\\""; break;
                case '\\': result += "\\\\"; break;
                case '\n': result += "\\n"; break;
                case '\r': result += "\\r"; break;
                case '\t': result += "\\t"; break;
                default:
                    if (c < 32) {
                        char buf[7];
                        snprintf(buf, sizeof(buf), "\\u%04x", (unsigned)c);
                        result += buf;
                    } else {
                        result += c;
                    }
            }
        }
        return result;
    }
};

#endif // SERVER_STATS_HPP
