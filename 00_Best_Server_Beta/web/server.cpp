// Web Server Example - File Transfer & WebSocket Support
// 
// Demonstrates:
// - HTTP file upload/download
// - WebSocket for real-time communication (video/voice)
// - Static file serving

#include <best_server/best_server.hpp>
#include <best_server/network/websocket/websocket.hpp>
#include <best_server/core/thread_pool.hpp>
#include <best_server/monitoring/system_monitor.hpp>
#include <iostream>
#include <fstream>
#include <sstream>
#include <filesystem>
#include <queue>
#include <fcntl.h>
#include <unistd.h>
#include <cerrno>
#include <cstring>
#include <thread>
#include <chrono>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <ifaddrs.h>
#include <net/if.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <csignal>
#include <atomic>
#include <vector>
#include <set>
#include <algorithm>
#include <archive.h>
#include <archive_entry.h>
#include "server_stats.hpp"

using namespace best_server;
using namespace best_server::network;

namespace fs = std::filesystem;

// 全局服务器状态
ServerStats g_server_stats;

// 路径配置相关
std::string g_config_file_path = "server_config.json";
std::string g_default_upload_path = "";
std::string g_custom_base_path = "";
std::string g_current_upload_path = "";

// 读取配置文件
bool load_config() {
    try {
        if (fs::exists(g_config_file_path)) {
            std::ifstream config_file(g_config_file_path);
            if (config_file.is_open()) {
                std::string line;
                while (std::getline(config_file, line)) {
                    // 简单的JSON解析，查找 customPath 字段
                    size_t pos = line.find("\"customPath\"");
                    if (pos != std::string::npos) {
                        size_t start = line.find("\"", pos + 13);
                        if (start != std::string::npos) {
                            start = line.find("\"", start + 1);
                            size_t end = line.find("\"", start + 1);
                            if (end != std::string::npos) {
                                g_custom_base_path = line.substr(start + 1, end - start - 1);
                                std::cout << "Loaded custom path: " << g_custom_base_path << std::endl;
                            }
                        }
                        break;
                    }
                }
                config_file.close();
                return true;
            }
        }
    } catch (const std::exception& e) {
        std::cerr << "Error loading config: " << e.what() << std::endl;
    }
    return false;
}

// 保存配置文件
bool save_config(const std::string& custom_path) {
    try {
        std::ofstream config_file(g_config_file_path);
        if (config_file.is_open()) {
            config_file << "{\n";
            config_file << "  \"customPath\": \"" << custom_path << "\"\n";
            config_file << "}\n";
            config_file.close();
            return true;
        }
    } catch (const std::exception& e) {
        std::cerr << "Error saving config: " << e.what() << std::endl;
    }
    return false;
}

// 验证路径有效性
bool is_path_valid(const std::string& path) {
    if (path.empty()) return false;
    
    std::string actual_path = path;
    
    // 支持@前缀，去掉@符号
    if (path[0] == '@') {
        actual_path = path.substr(1);
    }
    
    // 检查是否为绝对路径
    if (actual_path[0] != '/') return false;
    
    // 检查路径中不包含非法字符
    const std::string invalid_chars = "<>:\"|?*";
    for (char c : invalid_chars) {
        if (actual_path.find(c) != std::string::npos) {
            return false;
        }
    }
    
    // 尝试创建目录来验证路径是否可写
    try {
        std::string test_path = actual_path + "/uploads";
        std::error_code ec;
        fs::create_directories(test_path, ec);
        if (!ec) {
            // 目录创建成功，路径有效
            return true;
        }
    } catch (const std::exception& e) {
        return false;
    }
    
    return false;
}

// 获取实际路径（去掉@前缀）
std::string get_actual_path(const std::string& path) {
    if (path.empty()) return path;
    if (path[0] == '@') {
        return path.substr(1);
    }
    return path;
}

// 获取当前的上传路径
std::string get_upload_path() {
    if (!g_custom_base_path.empty()) {
        std::string actual_path = get_actual_path(g_custom_base_path);
        if (is_path_valid(g_custom_base_path)) {
            // 确保路径不以/结尾
            if (!actual_path.empty() && actual_path.back() == '/') {
                actual_path.pop_back();
            }
            return actual_path + "/uploads";
        }
    }
    return g_default_upload_path;
}

// 获取本机IP地址列表（IPv4 和 IPv6）- 使用 getifaddrs()，POSIX 标准函数
std::vector<std::string> get_local_ips() {
    std::vector<std::string> ips;
    struct ifaddrs *ifaddr, *ifa;
    
    // 获取所有网络接口地址
    if (getifaddrs(&ifaddr) == -1) {
        perror("getifaddrs");
        // 失败时至少返回回环地址
        ips.push_back("127.0.0.1");
        ips.push_back("::1");
        return ips;
    }
    
    // 遍历所有接口
    for (ifa = ifaddr; ifa != nullptr; ifa = ifa->ifa_next) {
        if (ifa->ifa_addr == nullptr) continue;
        
        int family = ifa->ifa_addr->sa_family;
        
        // 跳过回环接口
        if (ifa->ifa_flags & IFF_LOOPBACK) continue;
        
        // 跳过没有运行状态的接口
        if (!(ifa->ifa_flags & IFF_UP)) continue;
        
        // 处理 IPv4 地址
        if (family == AF_INET) {
            struct sockaddr_in *addr = (struct sockaddr_in *)ifa->ifa_addr;
            char ip[INET_ADDRSTRLEN];
            if (inet_ntop(AF_INET, &addr->sin_addr, ip, INET_ADDRSTRLEN)) {
                ips.push_back(ip);
            }
        }
        // 处理 IPv6 地址
        else if (family == AF_INET6) {
            struct sockaddr_in6 *addr = (struct sockaddr_in6 *)ifa->ifa_addr;
            char ip[INET6_ADDRSTRLEN];
            if (inet_ntop(AF_INET6, &addr->sin6_addr, ip, INET6_ADDRSTRLEN)) {
                // 跳过链路本地地址 (fe80::)
                std::string ip_str(ip);
                if (ip_str.substr(0, 4) != "fe80") {
                    ips.push_back(ip);
                }
            }
        }
    }
    
    freeifaddrs(ifaddr);
    
    // 如果没有找到任何 IP 地址，至少添加回环地址
    if (ips.empty()) {
        ips.push_back("127.0.0.1");
        ips.push_back("::1");
    }
    
    return ips;
}

// 崩溃日志记录
std::string crash_log_file = "crash.log";
std::ofstream crash_log;

void log_crash(const std::string& reason) {
    crash_log.open(crash_log_file, std::ios::app);
    if (crash_log.is_open()) {
        auto now = std::chrono::system_clock::now();
        auto time = std::chrono::system_clock::to_time_t(now);
        crash_log << "\n========================================\n";
        crash_log << "CRASH DETECTED\n";
        crash_log << "Time: " << std::ctime(&time);
        crash_log << "Reason: " << reason << "\n";
        crash_log << "========================================\n";
        crash_log.close();
    }
    std::cerr << "CRASH: " << reason << std::endl;
}

// 信号处理函数
void signal_handler(int signal) {
    std::string reason;
    switch (signal) {
        case SIGSEGV: reason = "Segmentation Fault (SIGSEGV)"; break;
        case SIGABRT: reason = "Abort (SIGABRT)"; break;
        case SIGFPE:  reason = "Floating Point Exception (SIGFPE)"; break;
        case SIGILL:  reason = "Illegal Instruction (SIGILL)"; break;
        case SIGTERM: reason = "Termination (SIGTERM) - Normal shutdown"; return;
        case SIGINT:  reason = "Interrupt (SIGINT) - Normal shutdown"; return;
        default:      reason = "Unknown signal (" + std::to_string(signal) + ")"; break;
    }
    
    log_crash(reason);
    
    // 重新执行程序实现自动重启
    std::cerr << "Attempting to restart server..." << std::endl;
    execv("/proc/self/exe", nullptr);
}

// 高性能并行打包函数（使用 zstd 快速压缩）
bool parallel_pack_folder(const std::string& folder_path, const std::string& output_path) {
    // 使用 zstd -T4（4线程）快速压缩，速度比 xz 快 10-30 倍
    // -3 是快速压缩级别，在速度和压缩率之间平衡
    std::string command = "cd \"" + folder_path + "\" && tar cf - . | zstd -T4 -3 -f -o \"" + output_path + "\"";
    int result = system(command.c_str());
    
    return result == 0;
}

// 清理 .temp 文件夹中超过10分钟且未被使用的文件
void cleanup_temp_files(const std::string& temp_dir) {
    std::error_code ec;
    
    if (!fs::exists(temp_dir, ec)) {
        return;
    }
    
    auto now = fs::file_time_type::clock::now();
    
    for (const auto& entry : fs::directory_iterator(temp_dir, ec)) {
        if (entry.is_regular_file(ec)) {
            std::string filename = entry.path().filename().string();
            std::string file_path = entry.path().string();
            
            // 跳过锁文件本身
            if (filename.find(".lock") != std::string::npos || 
                filename.find(".download") != std::string::npos) {
                continue;
            }
            
            // 检查是否有对应的锁文件（说明正在使用）
            std::string lock_file = file_path + ".lock";
            std::string download_lock_file = file_path + ".download";
            
            if (fs::exists(lock_file, ec) || fs::exists(download_lock_file, ec)) {
                // 文件正在使用中（打包中或下载中），跳过删除
                continue;
            }
            
            // 计算文件年龄
            auto ftime = entry.last_write_time(ec);
            auto age = std::chrono::duration_cast<std::chrono::seconds>(now - ftime).count();
            
            // 超过10分钟且未被使用的文件才删除
            if (age > 600) {
                std::cout << "删除过期临时文件: " << filename 
                          << " (年龄: " << age << " 秒)" << std::endl;
                fs::remove(file_path, ec);
            }
        }
    }
}

// URL解码函数
std::string url_decode(const std::string& encoded) {
    std::string result;
    for (size_t i = 0; i < encoded.length(); ++i) {
        if (encoded[i] == '%' && i + 2 < encoded.length()) {
            std::string hex = encoded.substr(i + 1, 2);
            char decoded = static_cast<char>(std::strtol(hex.c_str(), nullptr, 16));
            result += decoded;
            i += 2;
        } else if (encoded[i] == '+') {
            result += ' ';
        } else {
            result += encoded[i];
        }
    }
    return result;
}

// JSON字符串转义函数
std::string escape_json_string(const std::string& str) {
    std::string result;
    for (char c : str) {
        switch (c) {
            case '"':  result += R"(\")"; break;
            case '\\': result += R"(\\)"; break;
            case '\b': result += R"(\b)"; break;
            case '\f': result += R"(\f)"; break;
            case '\n': result += R"(\n)"; break;
            case '\r': result += R"(\r)"; break;
            case '\t': result += R"(\t)"; break;
            default:
                if (c < 0x20) {
                    result += "\\u";
                    char buf[5];
                    snprintf(buf, sizeof(buf), "%04X", static_cast<unsigned char>(c));
                    result += buf;
                } else {
                    result += c;
                }
        }
    }
    return result;
}

// 计算文件夹总大小（BFS方式）
size_t calculate_directory_size(const fs::path& dir_path, std::error_code& ec) {
    size_t total_size = 0;
    
    std::queue<fs::path> dir_queue;
    dir_queue.push(dir_path);
    
    while (!dir_queue.empty()) {
        fs::path current_dir = dir_queue.front();
        dir_queue.pop();
        
        for (const auto& entry : fs::directory_iterator(current_dir, ec)) {
            if (entry.is_directory()) {
                // 将子目录加入队列
                dir_queue.push(entry.path());
            } else if (entry.is_regular_file()) {
                size_t file_size = fs::file_size(entry.path(), ec);
                total_size += file_size;
            }
        }
    }
    
    return total_size;
}

// Static file serving
void serve_static_file(HTTPRequest& req, HTTPResponse& res, const std::string& base_path) {
    std::string url_path = req.path();
    if (url_path == "/") url_path = "/index.html";
    
    std::string file_path = base_path + url_path;
    
    if (file_path.find("..") != std::string::npos) {
        res.set_status(HTTPStatus::Forbidden);
        res.set_text("Access denied");
        return;
    }
    
    if (!fs::exists(file_path)) {
        res.set_status(HTTPStatus::NotFound);
        res.set_text("File not found: " + url_path);
        return;
    }
    
    std::ifstream file(file_path, std::ios::binary);
    if (!file) {
        res.set_status(HTTPStatus::InternalServerError);
        res.set_text("Failed to read file");
        return;
    }
    
    file.seekg(0, std::ios::end);
    size_t file_size = file.tellg();
    file.seekg(0, std::ios::beg);
    
    std::vector<char> buffer(file_size);
    file.read(buffer.data(), file_size);
    
    std::string content_type = "application/octet-stream";
    if (url_path.ends_with(".html")) content_type = "text/html";
    else if (url_path.ends_with(".css")) content_type = "text/css";
    else if (url_path.ends_with(".js")) content_type = "application/javascript";
    else if (url_path.ends_with(".json")) content_type = "application/json";
    else if (url_path.ends_with(".png")) content_type = "image/png";
    else if (url_path.ends_with(".jpg") || url_path.ends_with(".jpeg")) content_type = "image/jpeg";
    
    res.set_status(HTTPStatus::OK);
    res.set_content_type(content_type);
    res.set_header("Content-Length", std::to_string(file_size));
    std::string content(buffer.data(), file_size);
    res.set_body(content);
}

// File upload handler - supports chunked upload for large files
void handle_file_upload(HTTPRequest& req, HTTPResponse& res, const std::string& upload_path) {
    if (req.method() != HTTPMethod::POST) {
        res.set_status(HTTPStatus::MethodNotAllowed);
        res.set_text("Only POST method is allowed");
        return;
    }
    
    std::string filename = url_decode(req.get_header("X-Filename", "upload.bin"));
    std::string filepath = url_decode(req.get_header("X-File-Path", ""));
    
    // 检查 filepath 是否被污染（如果包含 User-Agent，说明传输过程出错，可能是并行传输问题）
    if (filepath.find("User-Agent") != std::string::npos) {
        filepath.clear();
    }
    
    // Check if this is a chunked upload
    std::string chunk_index_str = req.get_header("X-Chunk-Index", "");
    bool is_chunked_upload = !chunk_index_str.empty();
    
    std::string file_path = upload_path;
    if (!filepath.empty()) {
        file_path += "/" + filepath;
    }
    file_path += "/" + filename;
    
    // Ensure directory exists
    std::string dir_path = file_path.substr(0, file_path.find_last_of('/'));
    std::error_code ec;
    fs::create_directories(dir_path, ec);
    if (ec) {
        g_server_stats.add_log("Failed to create directory: " + dir_path + " - " + ec.message(), "error");
        res.set_status(HTTPStatus::InternalServerError);
        res.set_text("Failed to create directory: " + ec.message());
        g_server_stats.add_response(500);
        return;
    }
    
    size_t body_size = req.body().size();
    size_t bytes_written = 0;
    
    if (is_chunked_upload) {
        // Chunked upload mode
        int chunk_index = std::stoi(chunk_index_str);
        int total_chunks = std::stoi(req.get_header("X-Total-Chunks", "1"));
        size_t chunk_offset = std::stoull(req.get_header("X-Chunk-Offset", "0"));
        
        // Use O_CREAT | O_WRONLY without O_TRUNC to preserve existing file
        int fd = open(file_path.c_str(), O_WRONLY | O_CREAT, 0644);
        if (fd < 0) {
            g_server_stats.add_log("Failed to create file: " + file_path + " - " + strerror(errno), "error");
            res.set_status(HTTPStatus::InternalServerError);
            res.set_text("Failed to create file: " + std::string(strerror(errno)));
            g_server_stats.add_response(500);
            return;
        }
        
        if (body_size > 0) {
            // Use pwrite to write at the specified offset
            ssize_t written = pwrite(fd, req.body().data(), body_size, chunk_offset);
            
            if (written < 0) {
                close(fd);
                g_server_stats.add_log("Failed to write chunk to file: " + file_path + " - " + strerror(errno), "error");
                res.set_status(HTTPStatus::InternalServerError);
                res.set_text("Failed to write chunk: " + std::string(strerror(errno)));
                g_server_stats.add_response(500);
                return;
            }
            
            bytes_written = written;
        }
        
        close(fd);
        
        g_server_stats.add_bytes_received(bytes_written);
        g_server_stats.add_log("Chunk uploaded: " + filename + " (chunk " + std::to_string(chunk_index) + "/" + 
                              std::to_string(total_chunks) + ", " + std::to_string(bytes_written) + " bytes)", "success");
        
        res.set_status(HTTPStatus::OK);
        g_server_stats.add_response(200);
        res.set_json(R"({"message": "Chunk uploaded successfully", "filename": ")" + filename + 
                     R"(", "chunk_index": )" + std::to_string(chunk_index) + 
                     R"(, "total_chunks": )" + std::to_string(total_chunks) + 
                     R"(, "bytes_written": )" + std::to_string(bytes_written) + R"(})");
    } else {
        // Legacy single-shot upload mode (backward compatibility)
        int fd = open(file_path.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
        if (fd < 0) {
            g_server_stats.add_log("Failed to create file: " + file_path + " - " + strerror(errno), "error");
            res.set_status(HTTPStatus::InternalServerError);
            res.set_text("Failed to create file: " + std::string(strerror(errno)));
            g_server_stats.add_response(500);
            return;
        }
        
        if (body_size > 0) {
            // Use pwrite to write directly
            ssize_t written = pwrite(fd, req.body().data(), body_size, 0);
            
            if (written < 0) {
                close(fd);
                g_server_stats.add_log("Failed to write file: " + file_path + " - " + strerror(errno), "error");
                res.set_status(HTTPStatus::InternalServerError);
                res.set_text("Failed to write file: " + std::string(strerror(errno)));
                g_server_stats.add_response(500);
                return;
            }
            
            bytes_written = written;
        }
        
        close(fd);
        
        g_server_stats.add_bytes_received(bytes_written);
        g_server_stats.add_log("File uploaded successfully: " + filename + " (" + std::to_string(bytes_written) + " bytes)", "success");
        
        res.set_status(HTTPStatus::OK);
        g_server_stats.add_response(200);
        std::string response_json = R"({"message": "File uploaded successfully", "filename": ")" + filename + 
                     R"(", "size": )" + std::to_string(bytes_written) + R"(})";
        res.set_json(response_json);
        g_server_stats.add_bytes_sent(response_json.size());
    }
}

// 文件夹上传处理 - 直接传输文件夹结构（不打包ZIP）
void handle_folder_upload(HTTPRequest& req, HTTPResponse& res, const std::string& upload_path) {
    if (req.method() != HTTPMethod::POST) {
        res.set_status(HTTPStatus::MethodNotAllowed);
        res.set_text("Only POST method is allowed");
        return;
    }
    
    std::string foldername = url_decode(req.get_header("X-Foldername", "uploaded_folder"));
    std::string relative_path = url_decode(req.get_header("X-File-Path", ""));
    
    auto form_fields = req.parse_multipart_form_data();
    
    size_t files_uploaded = 0;
    size_t total_size = 0;
    
    for (const auto& field : form_fields) {
        if (!field.filename.empty()) {
            std::string full_path = upload_path + "/" + foldername;
            
            if (!relative_path.empty()) {
                full_path += "/" + relative_path;
            }
            
            // 使用 std::filesystem 替代 system() 调用，更安全高效
            size_t last_slash = field.filename.find_last_of('/');
            if (last_slash != std::string::npos) {
                std::string dir_path = full_path + "/" + field.filename.substr(0, last_slash);
                std::error_code ec;
                fs::create_directories(dir_path, ec);
                if (ec) {
                    g_server_stats.add_log("Failed to create directory: " + dir_path + " - " + ec.message(), "error");
                }
            }
            
            std::string file_path = full_path + "/" + field.filename;
            
            // 使用简单的同步写入
            int fd = open(file_path.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
            if (fd >= 0) {
                size_t file_size = field.data.size();
                if (file_size > 0) {
                    ssize_t written = pwrite(fd, field.data.data(), file_size, 0);
                    
                    close(fd);
                    
                    if (written >= 0) {
                        files_uploaded++;
                        total_size += written;
                    } else {
                        g_server_stats.add_log("Failed to write file: " + file_path, "error");
                    }
                } else {
                    close(fd);
                    files_uploaded++;
                }
            } else {
                g_server_stats.add_log("Failed to open file: " + file_path, "error");
            }
        }
    }
    
    res.set_status(HTTPStatus::OK);
    res.set_json(R"({"message": "Folder uploaded successfully", "foldername": ")" + foldername + 
                 R"(", "files_uploaded": )" + std::to_string(files_uploaded) + 
                 R"(, "total_size": )" + std::to_string(total_size) + R"(})");
}

// 获取文件夹文件列表（BFS方式）
void handle_folder_list(HTTPRequest& req, HTTPResponse& res, const std::string& base_path) {
    std::string foldername = req.path().substr(13); // /list/folder/ 长度为 13
    std::string folder_path = base_path + "/" + foldername;
    
    // 如果文件夹名为空，使用根目录
    if (foldername.empty()) {
        folder_path = base_path;
    }
    
    std::error_code ec;
    if (!fs::exists(folder_path, ec) || !fs::is_directory(folder_path, ec)) {
        res.set_status(HTTPStatus::NotFound);
        res.set_text("Folder not found");
        return;
    }
    
    std::string json = R"({"foldername": ")" + foldername + R"(", "files": [)";
    
    bool first = true;
    
    // 使用BFS方式遍历目录
    std::queue<fs::path> dir_queue;
    dir_queue.push(folder_path);
    
    while (!dir_queue.empty()) {
        fs::path current_dir = dir_queue.front();
        dir_queue.pop();
        
        for (const auto& entry : fs::directory_iterator(current_dir, ec)) {
            if (entry.is_directory()) {
                // 将子目录加入队列
                dir_queue.push(entry.path());
            } else if (entry.is_regular_file()) {
                if (!first) json += ",";
                first = false;
                
                std::string full_path = entry.path().string();
                std::string relative_path = full_path.substr(folder_path.length() + 1);
                size_t file_size = fs::file_size(entry.path(), ec);
                
                json += R"({"path": ")" + relative_path + R"(", "size": )" + std::to_string(file_size) + "}";
            }
        }
    }
    
    json += "]}";
    res.set_status(HTTPStatus::OK);
    res.set_json(json);
}

// 文件夹下载处理 - 返回文件列表（BFS方式）
void handle_folder_download(HTTPRequest& req, HTTPResponse& res, const std::string& base_path) {
    std::string foldername = req.path().substr(17); // /download/folder/ 长度为 17
    std::string folder_path = base_path + "/" + foldername;
    
    // 如果文件夹名为空，使用根目录
    if (foldername.empty()) {
        folder_path = base_path;
    }
    
    std::error_code ec;
    if (!fs::exists(folder_path, ec) || !fs::is_directory(folder_path, ec)) {
        res.set_status(HTTPStatus::NotFound);
        res.set_text("Folder not found");
        return;
    }
    
    size_t total_files = 0;
    size_t total_directories = 0;
    size_t total_bytes = 0;
    std::string files_json = "[";
    bool first = true;
    
    // 使用BFS方式遍历目录
    std::queue<fs::path> dir_queue;
    dir_queue.push(folder_path);
    
    while (!dir_queue.empty()) {
        fs::path current_dir = dir_queue.front();
        dir_queue.pop();
        
        for (const auto& entry : fs::directory_iterator(current_dir, ec)) {
            if (entry.is_directory()) {
                total_directories++;
                // 将子目录加入队列
                dir_queue.push(entry.path());
            } else if (entry.is_regular_file()) {
                total_files++;
                size_t file_size = fs::file_size(entry.path(), ec);
                total_bytes += file_size;
                
                if (!first) files_json += ",";
                first = false;
                
                std::string full_path = entry.path().string();
                std::string relative_path = full_path.substr(folder_path.length() + 1);
                
                files_json += R"({"path": ")" + relative_path + R"(", "size": )" + std::to_string(file_size) + "}";
            }
        }
    }
    
    files_json += "]";
    
    std::string json = R"({"foldername": ")" + foldername + 
                       R"(", "total_files": )" + std::to_string(total_files) + 
                       R"(, "total_directories": )" + std::to_string(total_directories) + 
                       R"(, "total_bytes": )" + std::to_string(total_bytes) + 
                       R"(, "files": )" + files_json + "}";
    
    res.set_status(HTTPStatus::OK);
    res.set_json(json);
}

int main(int argc, char* argv[]) {
    // 设置崩溃日志文件路径
    fs::path exe_path = fs::canonical(fs::path(argv[0])).parent_path();
    crash_log_file = (exe_path / "crash.log").string();
    
    // 注册信号处理器用于崩溃检测
    signal(SIGSEGV, signal_handler);  // 段错误
    signal(SIGABRT, signal_handler);  // Abort
    signal(SIGFPE, signal_handler);   // 浮点异常
    signal(SIGILL, signal_handler);   // 非法指令
    
    std::cout << "Best_Server Web Server - File Transfer & WebSocket" << std::endl;
    std::cout << "=====================================================" << std::endl;
    
    try {
    // Parse command-line arguments
    bool enable_https = false;
    int port = 8080;
    std::string cert_file = "";
    std::string key_file = "";
    
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--https" || arg == "-s") {
            enable_https = true;
        } else if (arg == "--port" || arg == "-p") {
            if (i + 1 < argc) {
                port = std::stoi(argv[++i]);
            }
        } else if (arg == "--cert" || arg == "-c") {
            if (i + 1 < argc) {
                cert_file = argv[++i];
            }
        } else if (arg == "--key" || arg == "-k") {
            if (i + 1 < argc) {
                key_file = argv[++i];
            }
        } else if (arg == "--help" || arg == "-h") {
            std::cout << "Usage: " << argv[0] << " [options]" << std::endl;
            std::cout << "Options:" << std::endl;
            std::cout << "  --https, -s          Enable HTTPS" << std::endl;
            std::cout << "  --port, -p PORT      Set server port (default: 8080)" << std::endl;
            std::cout << "  --cert, -c FILE      Set certificate file for HTTPS" << std::endl;
            std::cout << "  --key, -k FILE       Set private key file for HTTPS" << std::endl;
            return 0;
        }
    }
    
    // 智能路径解析：通用结构检测
    fs::path exe_path = fs::canonical(fs::path(argv[0])).parent_path();
    fs::path exe_dir = exe_path;  // web_server 所在目录
    fs::path parent_dir = exe_path.parent_path();  // web_server 的父目录
    
    std::string base_path, upload_path;

    // 优先检查 web_server 所在目录的 static 中是否有 index.html（结构1）
    // 例如：web_server、static、uploads 在同一目录下
    if (fs::exists(exe_dir / "static" / "index.html")) {
        base_path = (exe_dir / "static").string();
        upload_path = (exe_dir / "uploads").string();
        std::cout << "Using current directory for static/uploads" << std::endl;
    }
    // 检查父目录的 static 中是否有 index.html（结构2）
    // 例如：web/build/web_server，static 在 web/ 目录下
    else if (fs::exists(parent_dir / "static" / "index.html")) {
        base_path = (parent_dir / "static").string();
        upload_path = (parent_dir / "uploads").string();
        std::cout << "Using parent directory for static/uploads" << std::endl;
    }
    // 使用当前工作目录作为备选
    else {
        fs::path current_dir = fs::current_path();
        base_path = (current_dir / "static").string();
        upload_path = (current_dir / "uploads").string();
        std::cout << "Using current working directory" << std::endl;
    }
    
    //创建上传目录
    fs::create_directories(upload_path);
    
    // 保存默认路径
    g_default_upload_path = upload_path;
    
    // 读取配置文件
    load_config();
    
    // 获取当前使用的上传路径
    g_current_upload_path = get_upload_path();
    
    // 确保上传路径存在
    fs::create_directories(g_current_upload_path);
    
    std::cout << "Static files: " << base_path << std::endl;
    std::cout << "Default upload path: " << g_default_upload_path << std::endl;
    std::cout << "Current upload path: " << g_current_upload_path << std::endl;
    
    HTTPServer server;
    
    HTTPServerConfig config;
    config.address = "0.0.0.0";
    config.port = port;
    config.enable_http2 = true;
    config.enable_websocket = true;
    config.enable_compression = true;
    config.max_request_size = 5ULL * 1024 * 1024 * 1024;  // 5GB
    config.max_response_size = 5ULL * 1024 * 1024 * 1024;  // 5GB
    config.max_connections = 10000000;  // 1000万连接
    config.max_requests_per_connection = 10000000;  // 1000万请求
    config.keep_alive_timeout_ms = 3600000;  // 60分钟
    
    // Enable HTTPS
    if (enable_https) {
        if (cert_file.empty() || key_file.empty()) {
            std::cerr << "Error: HTTPS requires both certificate and key files" << std::endl;
            std::cerr << "Usage: " << argv[0] << " --https --cert cert.pem --key key.pem" << std::endl;
            return 1;
        }
        
        config.enable_tls = true;
        config.tls_cert_file = cert_file;
        config.tls_key_file = key_file;
        
        server.enable_https(cert_file, key_file);
        std::cout << "HTTPS enabled with certificate: " << cert_file << std::endl;
        std::cout << "HTTPS enabled with key: " << key_file << std::endl;
    }
    
    // 重要：路由注册顺序很重要！
    // 特定路径必须在通配符路径之前注册
    
    // API路由 - 必须先注册
    
    // 路径配置 API - GET
    server.get("/api/path-config", [](HTTPRequest& /*req*/, HTTPResponse& res) {
        g_server_stats.add_request();
        
        std::string current_path = get_upload_path();
        std::string json = R"({
            "success": true,
            "customPath": ")" + escape_json_string(g_custom_base_path) + R"(",
            "currentPath": ")" + escape_json_string(current_path) + R"("
        })";
        
        g_server_stats.add_response(200);
        res.set_json(json);
        g_server_stats.add_bytes_sent(json.size());
    });
    
    // 路径配置 API - POST
    server.post("/api/path-config", [](HTTPRequest& req, HTTPResponse& res) {
        g_server_stats.add_request();
        g_server_stats.add_bytes_received(req.content_length());
        
        try {
            // 解析JSON请求体
            auto body_buffer = req.body();
            std::string body(body_buffer.data(), body_buffer.size());
            
            // 简单的JSON解析，查找 customPath 字段
            size_t pos = body.find("\"customPath\"");
            if (pos == std::string::npos) {
                std::string error_json = R"({
                    "success": false,
                    "message": "Missing customPath field"
                })";
                g_server_stats.add_response(400);
                res.set_json(error_json);
                g_server_stats.add_bytes_sent(error_json.size());
                return;
            }
            
            // 查找冒号
            size_t colon_pos = body.find(":", pos);
            if (colon_pos == std::string::npos) {
                std::string error_json = R"({
                    "success": false,
                    "message": "Invalid customPath format"
                })";
                g_server_stats.add_response(400);
                res.set_json(error_json);
                g_server_stats.add_bytes_sent(error_json.size());
                return;
            }
            
            // 查找值的开始引号
            size_t start = body.find("\"", colon_pos);
            if (start == std::string::npos) {
                std::string error_json = R"({
                    "success": false,
                    "message": "Invalid customPath format"
                })";
                g_server_stats.add_response(400);
                res.set_json(error_json);
                g_server_stats.add_bytes_sent(error_json.size());
                return;
            }
            
            // 查找值的结束引号
            size_t end = body.find("\"", start + 1);
            if (end == std::string::npos) {
                std::string error_json = R"({
                    "success": false,
                    "message": "Invalid customPath format"
                })";
                g_server_stats.add_response(400);
                res.set_json(error_json);
                g_server_stats.add_bytes_sent(error_json.size());
                return;
            }
            
            std::string new_custom_path = body.substr(start + 1, end - start - 1);
            
            // 验证路径
            if (new_custom_path.empty()) {
                // 清空自定义路径，使用默认路径
                g_custom_base_path = "";
                save_config("");
                std::string current_path = get_upload_path();
                std::string json = R"({
                    "success": true,
                    "customPath": "",
                    "currentPath": ")" + escape_json_string(current_path) + R"(",
                    "message": "Custom path cleared, using default path"
                })";
                g_server_stats.add_response(200);
                res.set_json(json);
                g_server_stats.add_bytes_sent(json.size());
                return;
            }
            
            if (!is_path_valid(new_custom_path)) {
                std::string error_json = R"({
                    "success": false,
                    "message": "Invalid path or path is not writable"
                })";
                g_server_stats.add_response(400);
                res.set_json(error_json);
                g_server_stats.add_bytes_sent(error_json.size());
                return;
            }
            
            // 保存配置
            g_custom_base_path = new_custom_path;
            if (save_config(new_custom_path)) {
                std::string current_path = get_upload_path();
                std::string actual_custom_path = get_actual_path(new_custom_path);
                std::string json = R"({
                    "success": true,
                    "customPath": ")" + escape_json_string(new_custom_path) + R"(",
                    "currentPath": ")" + escape_json_string(current_path) + R"(",
                    "message": "Path configuration saved successfully"
                })";
                g_server_stats.add_response(200);
                res.set_json(json);
                g_server_stats.add_bytes_sent(json.size());
            } else {
                std::string error_json = R"({
                    "success": false,
                    "message": "Failed to save configuration"
                })";
                g_server_stats.add_response(500);
                res.set_json(error_json);
                g_server_stats.add_bytes_sent(error_json.size());
            }
        } catch (const std::exception& e) {
            std::string error_json = R"({
                "success": false,
                "message": ")" + escape_json_string(e.what()) + R"("
            })";
            g_server_stats.add_response(500);
            res.set_json(error_json);
            g_server_stats.add_bytes_sent(error_json.size());
        }
    });
    
    server.get("/api/files", [](HTTPRequest& /*req*/, HTTPResponse& res) {
        g_server_stats.add_request();
        
        std::string current_upload_path = get_upload_path();
        std::string json = "[";
        bool first = true;
        
        try {
            for (const auto& entry : fs::directory_iterator(current_upload_path)) {
                if (!entry.is_regular_file()) continue;
                
                if (!first) json += ",";
                first = false;
                
                json += R"({"name": ")" + escape_json_string(entry.path().filename().string()) + R"(", "size": )" + 
                       std::to_string(fs::file_size(entry.path())) + "}";
            }
        } catch (const std::exception& e) {
            g_server_stats.add_log("Error listing files: " + std::string(e.what()), "error");
        }
        
        json += "]";
        
        g_server_stats.add_response(200);
        res.set_json(json);
    });
    
    // 服务器状态 API
    server.get("/api/status", [](HTTPRequest& req, HTTPResponse& res) {
        g_server_stats.add_request();
        g_server_stats.add_bytes_received(req.content_length());
        
        std::string status_json = g_server_stats.get_status_json();
        
        g_server_stats.add_response(200);
        res.set_json(status_json);
        g_server_stats.add_bytes_sent(status_json.size());
    });
    
    // 系统资源 API
    server.get("/api/system", [](HTTPRequest& /*req*/, HTTPResponse& res) {
        g_server_stats.add_request();
        
        auto metrics = best_server::monitoring::get_all_system_metrics();
        
        std::string json = R"({
            "memory": {
                "total_mb": )" + std::to_string(metrics.memory_total_mb) + R"(,
                "used_mb": )" + std::to_string(metrics.memory_used_mb) + R"(,
                "free_mb": )" + std::to_string(metrics.memory_free_mb) + R"(,
                "usage_percent": )" + std::to_string(metrics.memory_usage_percent) + R"(
            },
            "cpu": {
                "usage_percent": )" + std::to_string(metrics.cpu_usage_percent) + R"(,
                "load1": )" + std::to_string(metrics.cpu_load1) + R"(,
                "load5": )" + std::to_string(metrics.cpu_load5) + R"(,
                "load15": )" + std::to_string(metrics.cpu_load15) + R"(,
                "cores": )" + std::to_string(metrics.cpu_cores) + R"(
            },
            "temperature": {
                "celsius": )" + std::to_string(metrics.device_temps.body) + R"(,
                "available": )" + (metrics.device_temps.body > 0 ? "true" : "false") + R"(
            },
            "gpu": {
                "usage_percent": )" + std::to_string(metrics.gpu_usage_percent) + R"(,
                "temperature": )" + std::to_string(metrics.gpu_temperature) + R"(,
                "available": )" + (metrics.gpu_usage_percent > 0 ? "true" : "false") + R"(
            },
            "uptime_seconds": )" + std::to_string(metrics.system_uptime_seconds) + R"(
        })";
        
        g_server_stats.add_response(200);
        res.set_json(json);
        g_server_stats.add_bytes_sent(json.size());
    });
    
    // 性能监控 API
    server.get("/api/performance", [](HTTPRequest& /*req*/, HTTPResponse& res) {
        g_server_stats.add_request();
        
        auto metrics = best_server::monitoring::get_all_system_metrics();
        
        // 如果系统网络流量为0，使用ServerStats的统计
        uint64_t upload_speed = metrics.network_sent_bytes_per_sec;
        uint64_t download_speed = metrics.network_received_bytes_per_sec;
        
        static uint64_t prev_server_bytes_sent = 0;
        static uint64_t prev_server_bytes_received = 0;
        static auto prev_time = std::chrono::steady_clock::now();
        static bool initialized = false;
        
        auto current_time = std::chrono::steady_clock::now();
        auto time_diff = std::chrono::duration_cast<std::chrono::seconds>(current_time - prev_time).count();
        
        // 如果系统流量为0，使用ServerStats作为后备
        if (upload_speed == 0 && download_speed == 0 && initialized && time_diff > 0) {
            uint64_t current_bytes_sent = g_server_stats.get_bytes_sent();
            uint64_t current_bytes_received = g_server_stats.get_bytes_received();
            
            if (current_bytes_sent >= prev_server_bytes_sent) {
                upload_speed = (current_bytes_sent - prev_server_bytes_sent) / time_diff;
            }
            if (current_bytes_received >= prev_server_bytes_received) {
                download_speed = (current_bytes_received - prev_server_bytes_received) / time_diff;
            }
        }
        
        prev_server_bytes_sent = g_server_stats.get_bytes_sent();
        prev_server_bytes_received = g_server_stats.get_bytes_received();
        prev_time = current_time;
        initialized = true;
        
        std::string json = R"({
            "upload_speed": )" + std::to_string(upload_speed) + R"(,
            "download_speed": )" + std::to_string(download_speed) + R"(,
            "disk_iops_read": )" + std::to_string(metrics.disk_read_ios) + R"(,
            "disk_iops_write": )" + std::to_string(metrics.disk_write_ios) + R"(,
            "disk_usage": )" + std::to_string(metrics.disk_usage_percent) + R"(,
            "cpu_temp": )" + std::to_string(metrics.cpu_temperature) + R"(,
            "gpu_temp": )" + std::to_string(metrics.gpu_temperature) + R"(,
            "disk_read_speed": )" + std::to_string(metrics.disk_read_bytes_per_sec) + R"(,
            "disk_write_speed": )" + std::to_string(metrics.disk_write_bytes_per_sec) + R"(
        })";
        
        g_server_stats.add_response(200);
        res.set_json(json);
        g_server_stats.add_bytes_sent(json.size());
    });
    
    // 设备温度 API
    server.get("/api/device-temperatures", [](HTTPRequest& /*req*/, HTTPResponse& res) {
        g_server_stats.add_request();
        
        auto metrics = best_server::monitoring::get_all_system_metrics();
        auto& temps = metrics.device_temps;
        
        std::string json = R"({
            "cpu": )" + std::to_string(temps.cpu) + R"(,
            "gpu": )" + std::to_string(temps.gpu) + R"(,
            "battery": )" + std::to_string(temps.battery) + R"(,
            "memory": )" + std::to_string(temps.memory) + R"(,
            "storage": )" + std::to_string(temps.storage) + R"(,
            "display": )" + std::to_string(temps.display) + R"(,
            "wifi": )" + std::to_string(temps.wifi) + R"(,
            "charger": )" + std::to_string(temps.charger) + R"(,
            "camera": )" + std::to_string(temps.camera) + R"(,
            "video": )" + std::to_string(temps.video) + R"(,
            "body": )" + std::to_string(temps.body) + R"(
        })";
        
        g_server_stats.add_response(200);
        res.set_json(json);
        g_server_stats.add_bytes_sent(json.size());
    });
    
    // 服务器资源 API
    server.get("/api/server-metrics", [](HTTPRequest& /*req*/, HTTPResponse& res) {
        g_server_stats.add_request();
        
        auto metrics = best_server::monitoring::get_all_system_metrics();
        double process_memory_mb = best_server::monitoring::get_process_memory();
        double process_virtual_memory_gb = best_server::monitoring::get_process_virtual_memory();
        
        std::string json = R"({
            "cpu_usage_percent": )" + std::to_string(metrics.cpu_usage_percent) + R"(,
            "memory_usage": )" + std::to_string(process_memory_mb) + R"(,
            "virtual_memory": )" + std::to_string(process_virtual_memory_gb) + R"(,
            "thread_count": )" + std::to_string(metrics.thread_count) + R"(,
            "open_files": )" + std::to_string(metrics.open_file_count) + R"(,
            "process_state": ")" + metrics.process_state + R"(",
            "uptime": )" + std::to_string(metrics.process_uptime_seconds) + R"(,
            "cpu_cores": )" + std::to_string(metrics.cpu_cores) + R"(,
            "pid": )" + std::to_string(getpid()) + R"(
        })";
        
        g_server_stats.add_response(200);
        res.set_json(json);
    });
    
    // 服务器日志 API
    server.get("/api/logs", [](HTTPRequest& /*req*/, HTTPResponse& res) {
        g_server_stats.add_request();
        g_server_stats.add_response(200);
        res.set_json(g_server_stats.get_logs_json());
    });
    
    // 递归浏览文件夹结构（新增）
    server.get("/api/browse", [](HTTPRequest& req, HTTPResponse& res) {
        g_server_stats.add_request();
        
        std::string current_upload_path = get_upload_path();
        std::string folderpath = req.get_query_param("path", "");
        
        // 如果路径为空，使用根目录
        if (folderpath.empty() || folderpath == "/") {
            folderpath = "";
        }
        
        std::string full_path = current_upload_path;
        if (!folderpath.empty()) {
            full_path += "/" + folderpath;
        }
        
        std::error_code ec;
        if (!fs::exists(full_path, ec)) {
            res.set_status(HTTPStatus::NotFound);
            res.set_json(R"({"error": "Path not found", "path": ")" + folderpath + R"("})");
            g_server_stats.add_response(404);
            return;
        }
        
        std::string json = R"({"path": ")" + folderpath + R"(", "items": [)";
        
        bool first = true;
        
        if (fs::is_directory(full_path, ec)) {
            // 列出目录内容
            for (const auto& entry : fs::directory_iterator(full_path, ec)) {
                if (!first) json += ",";
                first = false;
                
                std::string name = escape_json_string(entry.path().filename().string());
                bool is_dir = entry.is_directory(ec);
                size_t size = 0;
                
                if (is_dir) {
                    // 计算文件夹总大小
                    std::error_code size_ec;
                    size = calculate_directory_size(entry.path(), size_ec);
                } else {
                    size = fs::file_size(entry.path(), ec);
                }
                
                json += R"({"name": ")" + name + R"(", "is_dir": )" + std::to_string(is_dir) + 
                       R"(, "size": )" + std::to_string(size) + "}";
            }
        } else {
            // 单个文件
            std::string name = fs::path(full_path).filename().string();
            size_t size = fs::file_size(full_path, ec);
            
            json += R"({"name": ")" + escape_json_string(name) + R"(", "is_dir": false, "size": )" + std::to_string(size) + "}";
        }
        
        json += "]}";
        
        res.set_status(HTTPStatus::OK);
        res.set_json(json);
        g_server_stats.add_response(200);
    });
    
    // 文件夹打包下载API（多进程并行打包成 tar.gz）- HEAD请求用于检查状态
    server.head("/api/folder/download/{path:.*}", [upload_path](HTTPRequest& req, HTTPResponse& res) {
        g_server_stats.add_request();
        
        std::string folder_path = url_decode(req.path().substr(21)); // "/api/folder/download/" 的长度
        
        // 移除前导斜杠
        if (!folder_path.empty() && folder_path[0] == '/') {
            folder_path = folder_path.substr(1);
        }
        
        std::string current_upload_path = get_upload_path();
        std::string full_path = current_upload_path;
        if (!folder_path.empty()) {
            full_path += "/" + folder_path;
        }
        
        std::error_code ec;
        if (!fs::exists(full_path, ec) || !fs::is_directory(full_path, ec)) {
            res.set_status(HTTPStatus::NotFound);
            g_server_stats.add_response(404);
            return;
        }
        
        // 获取文件夹名作为压缩包名
        std::string folder_name = fs::path(full_path).filename().string();
        std::string tar_name = folder_name + ".tar.zst";
        std::string tar_path = current_upload_path + "/.temp/" + tar_name;
        std::string tar_temp_path = tar_path + ".temp"; // 临时文件路径
        std::string lock_file = tar_path + ".lock";
        
        // 创建临时目录
        std::string temp_dir = current_upload_path + "/.temp";
        if (!fs::exists(temp_dir, ec)) {
            fs::create_directories(temp_dir, ec);
        }
        
        // 检查压缩包是否已存在（完整的文件，不是临时文件）
        if (fs::exists(tar_path, ec)) {
            // 压缩包已存在且完整，返回200和文件大小
            size_t tar_size = fs::file_size(tar_path, ec);
            res.set_header("Content-Length", std::to_string(tar_size));
            res.set_header("Content-Type", "application/zstd");
            res.set_header("Content-Disposition", "attachment; filename=\"" + folder_name + ".tar.zst\"");
            res.set_header("Accept-Ranges", "bytes");
            res.set_status(HTTPStatus::OK);
            g_server_stats.add_response(200);
            return;
        }
        
        // 检查是否正在打包
        if (fs::exists(lock_file, ec) || fs::exists(tar_temp_path, ec)) {
            // 正在打包中，返回 202 Accepted
            res.set_status(HTTPStatus::Accepted);
            g_server_stats.add_response(202);
            return;
        }
        
        // 创建锁文件，开始打包
        std::ofstream lock(lock_file);
        lock.close();
        
        // 使用高性能并行打包
        std::cout << "开始后台打包: " << folder_name << std::endl;
        
        // 捕获当前上传路径
        std::string captured_upload_path = current_upload_path;
        
        std::thread([captured_upload_path, folder_path, tar_path, tar_temp_path, lock_file, folder_name]() {
            std::cout << "正在打包: " << folder_name << std::endl;
            
            std::string full_folder_path = captured_upload_path;
            if (!folder_path.empty()) {
                full_folder_path += "/" + folder_path;
            }
            
            // 先打包到临时文件
            bool success = parallel_pack_folder(full_folder_path, tar_temp_path);
            
            if (success) {
                // 使用原子重命名操作，确保文件完整性
                std::error_code rename_ec;
                std::filesystem::rename(tar_temp_path, tar_path, rename_ec);
                
                if (rename_ec) {
                    std::cerr << "重命名失败: " << rename_ec.message() << std::endl;
                    // 重命名失败，删除临时文件
                    std::filesystem::remove(tar_temp_path);
                } else {
                    std::cout << "打包完成: " << folder_name << std::endl;
                }
            } else {
                std::cout << "打包失败: " << folder_name << std::endl;
                // 打包失败，删除临时文件
                std::filesystem::remove(tar_temp_path);
            }
            
            // 删除锁文件
            std::filesystem::remove(lock_file);
        }).detach();
        
        // 返回 202 Accepted，告诉客户端正在打包
        res.set_status(HTTPStatus::Accepted);
        g_server_stats.add_response(202);
    });
    
    // 文件夹打包下载API（多进程并行打包成 tar.gz）- GET请求用于实际下载
    server.get("/api/folder/download/{path:.*}", [](HTTPRequest& req, HTTPResponse& res) {
        g_server_stats.add_request();
        
        std::string folder_path = url_decode(req.path().substr(21)); // "/api/folder/download/" 的长度
        
        // 移除前导斜杠
        if (!folder_path.empty() && folder_path[0] == '/') {
            folder_path = folder_path.substr(1);
        }
        
        std::string current_upload_path = get_upload_path();
        std::string full_path = current_upload_path;
        if (!folder_path.empty()) {
            full_path += "/" + folder_path;
        }
        
        std::error_code ec;
        if (!fs::exists(full_path, ec) || !fs::is_directory(full_path, ec)) {
            res.set_status(HTTPStatus::NotFound);
            res.set_json(R"({"error": "Folder not found", "path": ")" + folder_path + R"("})");
            g_server_stats.add_response(404);
            return;
        }
        
        // 获取文件夹名作为压缩包名
        std::string folder_name = fs::path(full_path).filename().string();
        std::string tar_name = folder_name + ".tar.zst";
        std::string tar_path = current_upload_path + "/.temp/" + tar_name;
        std::string tar_temp_path = tar_path + ".temp"; // 临时文件路径
        std::string lock_file = tar_path + ".lock";
        
        // 创建临时目录
        std::string temp_dir = current_upload_path + "/.temp";
        if (!fs::exists(temp_dir, ec)) {
            fs::create_directories(temp_dir, ec);
        }
        
        // 检查压缩包是否已存在（完整的文件，不是临时文件）
        if (fs::exists(tar_path, ec)) {
            // 压缩包已存在且完整，直接下载
            size_t tar_size = fs::file_size(tar_path, ec);
            res.set_header("Content-Length", std::to_string(tar_size));
            res.set_header("Content-Type", "application/zstd");
            res.set_header("Content-Disposition", "attachment; filename=\"" + folder_name + ".tar.zst\"");
            res.set_header("Accept-Ranges", "bytes");
            res.set_status(HTTPStatus::OK);
            
            // 创建下载锁文件，防止在下载过程中被清理和计时
            std::ofstream download_lock(tar_path + ".download");
            download_lock.close();
            
            res.enable_stream_file(tar_path);
            
            g_server_stats.add_bytes_sent(tar_size);
            g_server_stats.add_log("Folder download started (cached): " + folder_name + " (size: " + std::to_string(tar_size) + " bytes)", "success");
            std::cout << "正在发送压缩包: " << tar_path << " (" << tar_size << " bytes)" << std::endl;
            g_server_stats.add_response(200);
            return;
        }
        
        // 检查是否正在打包
        if (fs::exists(lock_file, ec) || fs::exists(tar_temp_path, ec)) {
            // 正在打包中，返回 202 Accepted
            res.set_status(HTTPStatus::Accepted);
            res.set_json(R"({"status": "packing", "message": "压缩包正在打包中，请稍后再试"})");
            g_server_stats.add_response(202);
            return;
        }
        
        // 创建锁文件，开始打包
        std::ofstream lock(lock_file);
        lock.close();
        
        // 使用高性能并行打包
        std::cout << "开始后台打包: " << folder_name << std::endl;
        
        // 捕获当前上传路径
        std::string captured_upload_path = current_upload_path;
        
        std::thread([captured_upload_path, folder_path, tar_path, tar_temp_path, lock_file, folder_name]() {
            std::cout << "正在打包: " << folder_name << std::endl;
            
            std::string full_folder_path = captured_upload_path;
            if (!folder_path.empty()) {
                full_folder_path += "/" + folder_path;
            }
            
            // 先打包到临时文件
            bool success = parallel_pack_folder(full_folder_path, tar_temp_path);
            
            if (success) {
                // 使用原子重命名操作，确保文件完整性
                std::error_code rename_ec;
                std::filesystem::rename(tar_temp_path, tar_path, rename_ec);
                
                if (rename_ec) {
                    std::cerr << "重命名失败: " << rename_ec.message() << std::endl;
                    // 重命名失败，删除临时文件
                    std::filesystem::remove(tar_temp_path);
                } else {
                    std::cout << "打包完成: " << folder_name << std::endl;
                }
            } else {
                std::cout << "打包失败: " << folder_name << std::endl;
                // 打包失败，删除临时文件
                std::filesystem::remove(tar_temp_path);
            }
            
            // 删除锁文件
            std::filesystem::remove(lock_file);
        }).detach();
        
        // 返回 202 Accepted，告诉客户端正在打包
        res.set_status(HTTPStatus::Accepted);
        res.set_json(R"({"status": "packing", "message": "压缩包正在打包中，请稍后再试"})");
        g_server_stats.add_response(202);
    });
    
    // File System API - 文件夹下载（返回文件列表和下载链接，支持 File System API 的浏览器）
    server.get("/api/filesystem/download/{path:.*}", [](HTTPRequest& req, HTTPResponse& res) {
        g_server_stats.add_request();
        
        std::string folder_path = url_decode(req.path().substr(25)); // "/api/filesystem/download/" 的长度
        
        // 移除前导斜杠
        if (!folder_path.empty() && folder_path[0] == '/') {
            folder_path = folder_path.substr(1);
        }
        
        std::string current_upload_path = get_upload_path();
        std::string full_path = current_upload_path;
        if (!folder_path.empty()) {
            full_path += "/" + folder_path;
        }
        
        std::error_code ec;
        if (!fs::exists(full_path, ec) || !fs::is_directory(full_path, ec)) {
            res.set_status(HTTPStatus::NotFound);
            res.set_json(R"({"error": "Folder not found", "path": ")" + escape_json_string(folder_path) + R"("})");
            g_server_stats.add_response(404);
            return;
        }
        
        // 获取文件夹名
        std::string folder_name = fs::path(full_path).filename().string();
        
        // 递归遍历文件夹
        size_t total_files = 0;
        size_t total_bytes = 0;
        std::string files_json = "[";
        bool first = true;
        
        std::queue<fs::path> dir_queue;
        dir_queue.push(full_path);
        
        while (!dir_queue.empty()) {
            fs::path current_dir = dir_queue.front();
            dir_queue.pop();
            
            for (const auto& entry : fs::directory_iterator(current_dir, ec)) {
                if (entry.is_directory()) {
                    // 将子目录加入队列
                    dir_queue.push(entry.path());
                } else if (entry.is_regular_file()) {
                    total_files++;
                    size_t file_size = fs::file_size(entry.path(), ec);
                    total_bytes += file_size;
                    
                    if (!first) files_json += ",";
                    first = false;
                    
                    std::string full_file_path = entry.path().string();
                    std::string relative_path = full_file_path.substr(full_path.length() + 1);
                    
                    // 转义文件名中的特殊字符
                    std::string escaped_path = escape_json_string(relative_path);
                    
                    // 创建下载 URL - 将路径中的斜杠编码为 %2F
                    std::string download_url_path;
                    if (!folder_path.empty()) {
                        download_url_path = folder_path + "/" + relative_path;
                    } else {
                        download_url_path = folder_name + "/" + relative_path;
                    }
                    
                    // URL 编码路径
                    std::string encoded_path = download_url_path;
                    size_t pos = 0;
                    while ((pos = encoded_path.find('/', pos)) != std::string::npos) {
                        encoded_path.replace(pos, 1, "%2F");
                        pos += 3;
                    }
                    
                    std::string download_url = "/api/filesystem/file/" + encoded_path;
                    
                    files_json += R"({"path": ")" + escaped_path + R"(", "size": )" + std::to_string(file_size) + 
                                 R"(, "download_url": ")" + escape_json_string(download_url) + R"("})";
                }
            }
        }
        
        files_json += "]";
        
        std::string json = R"({"foldername": ")" + escape_json_string(folder_name) + 
                           R"(", "path": ")" + escape_json_string(folder_path) + 
                           R"(", "total_files": )" + std::to_string(total_files) + 
                           R"(, "total_bytes": )" + std::to_string(total_bytes) + 
                           R"(, "files": )" + files_json + "}";
        
        res.set_status(HTTPStatus::OK);
        res.set_json(json);
        g_server_stats.add_response(200);
        g_server_stats.add_log("File System API - Folder list: " + folder_name + " (" + std::to_string(total_files) + " files)", "success");
    });
    
    // File System API - 单个文件下载（支持 File System API 的浏览器）
    server.get("/api/filesystem/file/{path:.*}", [](HTTPRequest& req, HTTPResponse& res) {
        g_server_stats.add_request();
        
        std::string file_path = url_decode(req.path().substr(21)); // "/api/filesystem/file/" 的长度
        
        // 移除前导斜杠
        if (!file_path.empty() && file_path[0] == '/') {
            file_path = file_path.substr(1);
        }
        
        std::string current_upload_path = get_upload_path();
        std::string full_path = current_upload_path + "/" + file_path;
        
        std::error_code ec;
        if (!fs::exists(full_path, ec) || fs::is_directory(full_path, ec)) {
            res.set_status(HTTPStatus::NotFound);
            res.set_json(R"({"error": "File not found", "path": ")" + escape_json_string(file_path) + R"("})");
            g_server_stats.add_response(404);
            return;
        }
        
        size_t file_size = fs::file_size(full_path, ec);
        std::string filename = fs::path(full_path).filename().string();
        
        // 检查 Range header（支持断点续传）
        auto range = req.get_range();
        size_t start = 0;
        size_t end = file_size - 1;
        bool is_range_request = false;
        
        if (range.valid) {
            start = range.start;
            end = range.end;
            if (end >= file_size) {
                end = file_size - 1;
            }
            is_range_request = true;
        }
        
        size_t content_length = end - start + 1;
        
        // 设置响应头
        res.set_header("Content-Disposition", "attachment; filename=\"" + filename + "\"");
        res.set_header("Content-Type", "application/octet-stream");
        res.set_header("Accept-Ranges", "bytes");
        res.set_header("Content-Length", std::to_string(content_length));
        
        if (is_range_request) {
            res.set_status(HTTPStatus::PartialContent);
            res.set_header("Content-Range", "bytes " + std::to_string(start) + "-" + 
                          std::to_string(end) + "/" + std::to_string(file_size));
        } else {
            res.set_status(HTTPStatus::OK);
        }
        
        // 使用流式文件传输
        res.enable_stream_file(full_path);
        if (is_range_request) {
            res.set_stream_range(start, end);
        }
        
        g_server_stats.add_bytes_sent(content_length);
        g_server_stats.add_log("File System API - File download: " + filename + 
                              " (range: " + std::to_string(start) + "-" + std::to_string(end) + ")", "success");
        g_server_stats.add_response(is_range_request ? 206 : 200);
    });
    
    // 创建文件夹API（新增）
    // 文件下载路由（支持断点续传）- 必须在通配符之前

        server.get("/files/{filename}", [](HTTPRequest& req, HTTPResponse& res) {

            g_server_stats.add_request();

            std::string filename = url_decode(req.path().substr(7));

            std::string current_upload_path = get_upload_path();
            std::string file_path = current_upload_path + "/" + filename;
    
            
    
            if (!fs::exists(file_path)) {
    
                g_server_stats.add_log("File not found: " + filename, "error");
    
                res.set_status(HTTPStatus::NotFound);
    
                res.set_json(R"({"error": "File not found", "path": ")" + filename + R"("})");
                g_server_stats.add_response(404);
                return;
            }

            size_t file_size = fs::file_size(file_path);

            // 检查Range header
            auto range = req.get_range();
            size_t start = 0;
            size_t end = file_size - 1;
            bool is_range_request = false;

            if (range.valid) {
                start = range.start;
                end = range.end;
                if (end >= file_size) {
                    end = file_size - 1;
                }
                is_range_request = true;
            }

            // 记录Range请求到Range.txt
            /*
            {
                std::ofstream range_log("Range.txt", std::ios::app);
                if (range_log.is_open()) {
                    auto now = std::chrono::system_clock::now();
                    auto now_time = std::chrono::system_clock::to_time_t(now);
                    char time_str[100];
                    std::strftime(time_str, sizeof(time_str), "%Y-%m-%d %H:%M:%S", std::localtime(&now_time));
                    
                    std::string range_header = req.get_header("Range");
                    
                    range_log << "[" << time_str << "] File: " << filename 
                              << " | File size: " << file_size << " bytes"
                              << " | Range header: \"" << range_header << "\""
                              << " | Range valid: " << (range.valid ? "true" : "false")
                              << " | Range start: " << range.start 
                              << " | Range end: " << range.end 
                              << " | Calculated start: " << start 
                              << " | Calculated end: " << end
                              << " | Content length: " << (end - start + 1) << std::endl;
                }
            }
            */

            size_t content_length = end - start + 1;

            // 设置响应头，只使用文件名而不是完整路径
            std::string download_filename = fs::path(filename).filename().string();
            res.set_header("Content-Disposition", "attachment; filename=\"" + download_filename + "\"");
            res.set_header("Connection", "keep-alive");
            res.set_header("Accept-Ranges", "bytes");
            res.set_header("Content-Length", std::to_string(content_length));

            if (is_range_request) {
                res.set_status(HTTPStatus::PartialContent);
                res.set_header("Content-Range", "bytes " + std::to_string(start) + "-" + 
                              std::to_string(end) + "/" + std::to_string(file_size));
            } else {
                res.set_status(HTTPStatus::OK);
            }

            // 使用流式文件传输
            res.enable_stream_file(file_path);
            if (is_range_request) {
                res.set_stream_range(start, end);
            }

            g_server_stats.add_bytes_sent(content_length);
            g_server_stats.add_log("File download started: " + filename + 
                                  " (range: " + std::to_string(start) + "-" + std::to_string(end) + ")", "success");
            g_server_stats.add_response(is_range_request ? 206 : 200);
        });
    
            
    
    // 删除文件路由
    
    // 删除文件路由（已移除，使用新的统一删除API）
    
            
    
                        server.get("/", [base_path](HTTPRequest& req, HTTPResponse& res) {
    
            
    
                            serve_static_file(req, res, base_path);
    
            
    
                        });
    
            
    
                        
    
            
    
                        server.get("/index.html", [base_path](HTTPRequest& req, HTTPResponse& res) {
    
            
    
                        
    
            
    
                                    serve_static_file(req, res, base_path);
    
            
    
                        
    
            
    
                                });
    
            
    
                        
    
            
    
                                
    
            
    
                        
    
            
    
                                // 文件管理器页面
    
            
    
                        
    
            
    
                                server.get("/file_manager.html", [base_path](HTTPRequest& req, HTTPResponse& res) {
    
            
    
                        
    
            
    
                                    serve_static_file(req, res, base_path);
    
            
    
                        
    
            
    
                                });
    
            
    
                        
    
            
    
                        // 文件夹相关路由
    server.post("/upload", [base_path](HTTPRequest& req, HTTPResponse& res) {
        handle_file_upload(req, res, get_upload_path());
    });
    
    server.post("/upload/folder", [base_path](HTTPRequest& req, HTTPResponse& res) {
        handle_folder_upload(req, res, get_upload_path());
    });
    
    // 列出根目录的文件
    server.get("/list/folder/", [base_path](HTTPRequest& req, HTTPResponse& res) {
        handle_folder_list(req, res, base_path);
    });
    
    // 列出指定文件夹的文件
    server.get("/list/folder/{foldername}", [base_path](HTTPRequest& req, HTTPResponse& res) {
        handle_folder_list(req, res, base_path);
    });
    
    // 递归浏览文件夹结构 - 根目录（新增）
    server.get("/api/browse", [](HTTPRequest& req, HTTPResponse& res) {
        (void)req;  // Suppress unused parameter warning
        g_server_stats.add_request();
        
        std::string current_upload_path = get_upload_path();
        std::string full_path = current_upload_path;
        std::string folderpath = "";
        
        std::error_code ec;
        if (!fs::exists(full_path, ec)) {
            res.set_status(HTTPStatus::NotFound);
            res.set_json(R"({"error": "Path not found", "path": ")" + folderpath + R"("})");
            g_server_stats.add_response(404);
            return;
        }
        
        std::string json = R"({"path": ")" + folderpath + R"(", "items": [)";
        
        bool first = true;
        
        if (fs::is_directory(full_path, ec)) {
            // 列出目录内容
            for (const auto& entry : fs::directory_iterator(full_path, ec)) {
                if (!first) json += ",";
                first = false;
                
                std::string name = escape_json_string(entry.path().filename().string());
                bool is_dir = entry.is_directory(ec);
                size_t size = 0;
                
                if (is_dir) {
                    // 计算文件夹总大小
                    std::error_code size_ec;
                    size = calculate_directory_size(entry.path(), size_ec);
                } else {
                    size = fs::file_size(entry.path(), ec);
                }
                
                json += R"({"name": ")" + escape_json_string(name) + R"(", "is_dir": )" + std::to_string(is_dir) + 
                       R"(, "size": )" + std::to_string(size) + "}";
            }
        }
        
        json += "]}";
        
        res.set_status(HTTPStatus::OK);
        res.set_json(json);
        g_server_stats.add_response(200);
    });
    
    // 递归浏览文件夹结构（新增）
    server.get("/api/browse/{folderpath:.*}", [](HTTPRequest& req, HTTPResponse& res) {
        g_server_stats.add_request();
        
        std::string current_upload_path = get_upload_path();
        std::string folderpath = url_decode(req.path().substr(12)); // /api/browse/ 长度为12
        
        // 如果路径为空，使用根目录
        if (folderpath.empty() || folderpath == "/") {
            folderpath = "";
        }
        
        std::string full_path = current_upload_path;
        if (!folderpath.empty()) {
            full_path += "/" + folderpath;
        }
        
        std::error_code ec;
        if (!fs::exists(full_path, ec)) {
            res.set_status(HTTPStatus::NotFound);
            res.set_json(R"({"error": "Path not found", "path": ")" + folderpath + R"("})");
            g_server_stats.add_response(404);
            return;
        }
        
        std::string json = R"({"path": ")" + folderpath + R"(", "items": [)";
        
        bool first = true;
        
        if (fs::is_directory(full_path, ec)) {
            // 列出目录内容
            for (const auto& entry : fs::directory_iterator(full_path, ec)) {
                if (!first) json += ",";
                first = false;
                
                std::string name = escape_json_string(entry.path().filename().string());
                bool is_dir = entry.is_directory(ec);
                size_t size = 0;
                
                if (is_dir) {
                    // 计算文件夹总大小
                    std::error_code size_ec;
                    size = calculate_directory_size(entry.path(), size_ec);
                } else {
                    size = fs::file_size(entry.path(), ec);
                }
                
                json += R"({"name": ")" + name + R"(", "is_dir": )" + std::to_string(is_dir) + 
                       R"(, "size": )" + std::to_string(size) + "}";
            }
        } else {
            // 单个文件
            std::string name = fs::path(full_path).filename().string();
            size_t size = fs::file_size(full_path, ec);
            
            json += R"({"name": ")" + escape_json_string(name) + R"(", "is_dir": false, "size": )" + std::to_string(size) + "}";
        }
        
        json += "]}";
        
        res.set_status(HTTPStatus::OK);
        res.set_json(json);
        g_server_stats.add_response(200);
    });
    
    // 创建文件夹API（新增）
    server.post("/api/folder/create", [](HTTPRequest& req, HTTPResponse& res) {
        g_server_stats.add_request();
        
        // 记录所有请求头
        g_server_stats.add_log("=== 创建文件夹请求 ===", "info");
        auto headers = req.headers();
        for (const auto& header : headers) {
            g_server_stats.add_log("Header: " + header.first + " = " + header.second, "info");
        }
        
        // 尝试从body获取参数（URL编码格式）
        std::string foldername;
        std::string parent_path;
        
        const memory::ZeroCopyBuffer& body_buffer = req.body();
        std::string body_str(body_buffer.data(), body_buffer.size());
        
        size_t pos = 0;
        
        while (pos < body_str.length()) {
            size_t equals_pos = body_str.find('=', pos);
            size_t amp_pos = body_str.find('&', pos);
            
            if (equals_pos == std::string::npos) break;
            
            std::string key = body_str.substr(pos, equals_pos - pos);
            std::string value;
            
            if (amp_pos == std::string::npos) {
                value = body_str.substr(equals_pos + 1);
                pos = body_str.length();
            } else {
                value = body_str.substr(equals_pos + 1, amp_pos - equals_pos - 1);
                pos = amp_pos + 1;
            }
            
            if (key == "foldername") {
                foldername = url_decode(value);
            } else if (key == "parent_path") {
                parent_path = url_decode(value);
            }
        }
        
        // 如果FormData中没有数据，尝试从header获取（兼容旧版本）
        if (foldername.empty()) {
            foldername = url_decode(req.get_header("X-Foldername", "new_folder"));
        }
        if (parent_path.empty()) {
            std::string parent_path_raw = req.get_header("X-Parent-Path", "ROOT");
            parent_path = url_decode(parent_path_raw);
            // 如果是ROOT标识符，设为空字符串
            if (parent_path == "ROOT") {
                parent_path = "";
            }
        }
        
        // 验证parent_path，如果包含非法字符（如冒号、引号），则设为空
        if (parent_path.find(':') != std::string::npos || parent_path.find('"') != std::string::npos) {
            g_server_stats.add_log("警告: 父路径包含非法字符，设为空: '" + parent_path + "'", "error");
            parent_path = "";
        }
        
        g_server_stats.add_log("原始名称: '" + foldername + "' (长度: " + std::to_string(foldername.length()) + ")", "info");
        g_server_stats.add_log("解码后名称: '" + foldername + "' (长度: " + std::to_string(foldername.length()) + ")", "info");
        g_server_stats.add_log("父路径: '" + parent_path + "'", "info");
        
        std::string current_upload_path = get_upload_path();
        std::string full_path = current_upload_path;
        if (!parent_path.empty()) {
            full_path += "/" + parent_path;
        }
        full_path += "/" + foldername;
        
        g_server_stats.add_log("完整路径: '" + full_path + "'", "info");
        
        std::error_code ec;
        if (fs::exists(full_path, ec)) {
            res.set_status(HTTPStatus::Conflict);
            res.set_json(R"({"error": "Folder already exists", "path": ")" + foldername + R"("})");
            g_server_stats.add_response(409);
            g_server_stats.add_log("文件夹已存在: " + foldername, "error");
            return;
        }
        
        if (fs::create_directories(full_path, ec)) {
            res.set_status(HTTPStatus::Created);
            res.set_json(R"({"message": "Folder created successfully", "path": ")" + foldername + R"("})");
            g_server_stats.add_log("Folder created: " + foldername, "success");
            g_server_stats.add_response(201);
        } else {
            res.set_status(HTTPStatus::InternalServerError);
            res.set_json(R"({"error": "Failed to create folder", "message": ")" + ec.message() + R"("})");
            g_server_stats.add_response(500);
            g_server_stats.add_log("创建失败: " + ec.message(), "error");
        }
    });
    
    // 删除文件夹/文件API（新增）
    server.del("/api/files/{path:.*}", [](HTTPRequest& req, HTTPResponse& res) {
        g_server_stats.add_request();
        
        std::string path = url_decode(req.path().substr(11)); // /api/files/ 长度为11
        std::string current_upload_path = get_upload_path();
        std::string full_path = current_upload_path + "/" + path;
        
        std::error_code ec;
        if (!fs::exists(full_path, ec)) {
            res.set_status(HTTPStatus::NotFound);
            res.set_json(R"({"error": "Path not found", "path": ")" + escape_json_string(path) + R"("})");
            g_server_stats.add_response(404);
            return;
        }
        
        bool success = false;
        if (fs::is_directory(full_path, ec)) {
            success = fs::remove_all(full_path, ec);
        } else {
            success = fs::remove(full_path, ec);
        }
        
        if (success) {
            res.set_status(HTTPStatus::OK);
            res.set_json(R"({"message": "Deleted successfully", "path": ")" + escape_json_string(path) + R"("})");
            g_server_stats.add_log("Deleted: " + path, "success");
            g_server_stats.add_response(200);
        } else {
            res.set_status(HTTPStatus::InternalServerError);
            res.set_json(R"({"error": "Failed to delete", "message": ")" + escape_json_string(ec.message()) + R"("})");
            g_server_stats.add_response(500);
        }
    });
    
    // 下载根目录的文件列表
    server.get("/download/folder/", [](HTTPRequest& req, HTTPResponse& res) {
        handle_folder_download(req, res, get_upload_path());
    });
    
    // 下载指定文件夹的文件列表
    server.get("/download/folder/{foldername}", [](HTTPRequest& req, HTTPResponse& res) {
        handle_folder_download(req, res, get_upload_path());
    });
    
    // 静态文件路由 - 必须最后注册（作为fallback）
    // 暂时禁用，因为框架不支持真正的通配符路由
    // 用户需要直接访问完整的文件路径，例如：/index.html, /style.css 等
    // 如果需要添加静态文件支持，需要为每个文件单独注册路由，或者修改框架支持通配符
    
    // 注释掉通配符路由，因为框架不支持
    /*
    server.get(".*", [base_path](HTTPRequest& req, HTTPResponse& res) {
        std::string path = req.path();
        if (path.find("/api/") == 0 || path.find("/files/") == 0 || 
            path.find("/upload") == 0 || path.find("/list/folder/") == 0 ||
            path.find("/download/folder/") == 0) {
            res.set_status(HTTPStatus::NotFound);
            res.set_text("Endpoint not found");
            return;
        }
        serve_static_file(req, res, base_path);
    });
    */
    
    if (!server.start(config)) {
        std::cerr << "Failed to start server" << std::endl;
        return 1;
    }
    
    // 添加启动日志
    g_server_stats.add_log("Server started successfully", "success");
    g_server_stats.add_log("HTTP/2 enabled: " + std::string(config.enable_http2 ? "true" : "false"), "info");
    g_server_stats.add_log("WebSocket enabled: " + std::string(config.enable_websocket ? "true" : "false"), "info");
    
    // 启动清理线程，每30秒清理一次 .temp 文件夹
    std::string temp_dir = upload_path + "/.temp";
    std::thread cleanup_thread([temp_dir]() {
        while (true) {
            std::this_thread::sleep_for(std::chrono::seconds(30));
            cleanup_temp_files(temp_dir);
        }
    });
    cleanup_thread.detach();
    std::cout << "Temp file cleanup thread started (every 30 seconds)\n";
    
    std::cout << "\n========================================\n";
    std::cout << "Server started successfully!\n";
    std::cout << "========================================\n";
    std::cout << "Address: http://" << config.address << ":" << config.port << "\n";
    
    // 显示可访问的IP地址（IPv4 和 IPv6）
    std::vector<std::string> ips = get_local_ips();
    
    if (!ips.empty()) {
        std::cout << "\nAccess URLs:\n";
        std::set<std::string> unique_ips; // 去重
        
        for (const auto& ip : ips) {
            // 跳过已经显示过的IP
            if (unique_ips.count(ip) > 0) continue;
            unique_ips.insert(ip);
            
            // 检查是 IPv4 还是 IPv6
            if (ip.find(":") != std::string::npos) {
                // IPv6 地址
                std::cout << "  http://[" << ip << "]:" << config.port << "\n";
            } else {
                // IPv4 地址
                std::cout << "  http://" << ip << ":" << config.port << "\n";
            }
        }
    }
    
    std::cout << "\nStatic files: " << base_path << "\n";
    std::cout << "Upload path: " << upload_path << "\n";
    std::cout << "\nAPI Endpoints:\n";
    std::cout << "  POST  /upload              - Upload single file\n";
    std::cout << "  POST  /upload/folder       - Upload folder (direct structure)\n";
    std::cout << "  GET   /list/folder/*       - List folder contents\n";
    std::cout << "  GET   /download/folder/*   - Get folder file list\n";
    std::cout << "  GET   /files/*             - Download file (supports resume)\n";
    std::cout << "  GET   /api/files           - List files\n";
    std::cout << "  GET   /api/status          - Server status\n";
    std::cout << "  GET   /api/system          - System metrics\n";
    std::cout << "  GET   /api/logs            - Server logs\n";
    std::cout << "  GET   /api/folder/download/* - Pack and download folder (tar.zst)\n";
    std::cout << "  GET   /api/filesystem/download/* - Get folder file list for File System API\n";
    std::cout << "  GET   /api/filesystem/file/*   - Download single file for File System API\n";
    std::cout << "\nServer is running...\n";
    std::cout.flush();
    
    // 持续运行服务器
    while (server.is_running()) {
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }
    
    server.stop();
    
    std::cout << "\nServer stopped\n";
    
    return 0;
    
    } catch (const std::exception& e) {
        log_crash("Exception caught: " + std::string(e.what()));
        std::cerr << "Exception: " << e.what() << std::endl;
        std::cerr << "Attempting to restart server..." << std::endl;
        
        // 等待5秒后重启
        std::this_thread::sleep_for(std::chrono::seconds(5));
        
        // 使用 execv 重新执行程序
        std::vector<char*> argv_copy;
        for (int i = 0; i < argc; ++i) {
            argv_copy.push_back(argv[i]);
        }
        argv_copy.push_back(nullptr);
        
        execv(argv[0], argv_copy.data());
        
        // 如果 execv 失败
        std::cerr << "Failed to restart: " << strerror(errno) << std::endl;
        return 1;
    } catch (...) {
        log_crash("Unknown exception caught");
        std::cerr << "Unknown exception caught" << std::endl;
        std::cerr << "Attempting to restart server..." << std::endl;
        
        // 等待5秒后重启
        std::this_thread::sleep_for(std::chrono::seconds(5));
        
        // 使用 execv 重新执行程序
        std::vector<char*> argv_copy;
        for (int i = 0; i < argc; ++i) {
            argv_copy.push_back(argv[i]);
        }
        argv_copy.push_back(nullptr);
        
        execv(argv[0], argv_copy.data());
        
        // 如果 execv 失败
        std::cerr << "Failed to restart: " << strerror(errno) << std::endl;
        return 1;
    }
}
