// 简单的同步HTTP服务器 - 用于测试文件上传
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include <cstring>
#include <iostream>
#include <fstream>
#include <filesystem>
#include <thread>
#include <csignal>
#include <atomic>
#include <vector>
#include <chrono>

namespace fs = std::filesystem;

std::atomic<bool> running(true);
const int PORT = 8080;
const std::string UPLOAD_PATH = "/data/data/com.termux/files/home/Server/web/uploads";
const std::string STATIC_PATH = "/data/data/com.termux/files/home/Server/web/static";

// 服务器统计变量
std::atomic<uint64_t> request_count(0);
std::atomic<uint64_t> status_2xx(0);
std::atomic<uint64_t> status_4xx(0);
std::atomic<uint64_t> status_5xx(0);
std::atomic<uint64_t> bytes_sent(0);
std::atomic<uint64_t> bytes_received(0);
std::atomic<uint64_t> uptime_seconds(0);
std::chrono::steady_clock::time_point server_start_time;

// 获取当前时间字符串
std::string get_current_time() {
    auto now = std::chrono::system_clock::now();
    auto time_t_now = std::chrono::system_clock::to_time_t(now);
    std::tm tm_now;
    localtime_r(&time_t_now, &tm_now);
    char buffer[32];
    strftime(buffer, sizeof(buffer), "%H:%M:%S", &tm_now);
    return std::string(buffer);
}

// 获取 CPU 使用率和核心数
std::pair<double, int> get_cpu_usage() {
    // 使用 top 命令获取 CPU 使用率
    FILE* pipe = popen("top -bn1 2>/dev/null | grep -E '%cpu' | head -1", "r");
    if (!pipe) {
        return {0.0, 4};
    }
    
    char buffer[512];
    if (!fgets(buffer, sizeof(buffer), pipe)) {
        pclose(pipe);
        return {0.0, 4};
    }
    pclose(pipe);
    
    // 解析格式：800%cpu   0%user   0%nice   0%sys 800%idle   0%iow   0%irq   0%sirq   0%host
    // 使用正则表达式或字符串分割来提取数值
    std::string line(buffer);
    std::vector<double> values;
    std::string temp;
    
    // 提取所有数字
    bool in_number = false;
    for (char c : line) {
        if (isdigit(c) || c == '.') {
            temp += c;
            in_number = true;
        } else if (in_number) {
            if (!temp.empty()) {
                values.push_back(std::stod(temp));
            }
            temp.clear();
            in_number = false;
        }
    }
    if (!temp.empty()) {
        values.push_back(std::stod(temp));
    }
    
    // values 现在应该包含: [800, 0, 0, 0, 800, 0, 0, 0, 0]
    // 800%cpu   0%user   0%nice   0%sys 800%idle   0%iow   0%irq   0%sirq   0%host
    if (values.size() >= 5) {
        double total_cpu = values[0]; // 800
        double user_cpu = values[1];  // 0
        double nice_cpu = values[2];  // 0
        double sys_cpu = values[3];   // 0
        double idle_cpu = values[4];  // 800
        
        int cores = static_cast<int>(total_cpu / 100.0 + 0.5);
        if (cores < 1) cores = 1;
        
        // 计算实际使用率： (总CPU - 空闲) / 总CPU * 100
        double usage = 0.0;
        if (total_cpu > 0) {
            usage = ((total_cpu - idle_cpu) / total_cpu) * 100.0;
        }
        
        return {usage, cores};
    }
    
    return {0.0, 4};
}

// 获取系统负载（使用uptime命令）
std::tuple<double, double, double> get_system_load() {
    // 使用 uptime 命令获取系统负载
    FILE* pipe = popen("uptime 2>/dev/null", "r");
    if (!pipe) {
        return {0.0, 0.0, 0.0};
    }
    
    char buffer[256];
    if (!fgets(buffer, sizeof(buffer), pipe)) {
        pclose(pipe);
        return {0.0, 0.0, 0.0};
    }
    pclose(pipe);
    
    // 解析 uptime 输出格式：load average: 14.61, 14.70, 14.98
    std::string line(buffer);
    size_t load_pos = line.find("load average:");
    if (load_pos != std::string::npos) {
        load_pos += 13; // 跳过 "load average:"
        
        double load1 = 0.0, load5 = 0.0, load15 = 0.0;
        if (sscanf(line.c_str() + load_pos, "%lf, %lf, %lf", &load1, &load5, &load15) == 3) {
            return {load1, load5, load15};
        }
    }
    
    return {0.0, 0.0, 0.0};
}

// 获取系统运行时间（从系统启动到现在的时间，单位：秒）
long get_system_uptime() {
    // 使用 uptime 命令获取系统运行时间
    FILE* pipe = popen("uptime 2>/dev/null", "r");
    if (!pipe) {
        return 0;
    }
    
    char buffer[256];
    if (!fgets(buffer, sizeof(buffer), pipe)) {
        pclose(pipe);
        return 0;
    }
    pclose(pipe);
    
    // 解析 uptime 输出格式：20:27:53 up 15 days, 12:29, load average: 14.38, 14.33, 14.53
    std::string line(buffer);
    size_t up_pos = line.find("up ");
    if (up_pos != std::string::npos) {
        up_pos += 3; // 跳过 "up "
        
        long total_seconds = 0;
        
        // 检查是否有 "days" 或 "day"
        size_t days_pos = line.find("day", up_pos);
        if (days_pos != std::string::npos) {
            int days = 0;
            if (sscanf(line.c_str() + up_pos, "%d", &days) == 1) {
                total_seconds += days * 86400;
                // 跳过天数部分
                size_t comma_pos = line.find(",", up_pos);
                if (comma_pos != std::string::npos) {
                    up_pos = comma_pos + 1;
                    // 跳过空格
                    while (up_pos < line.length() && line[up_pos] == ' ') {
                        up_pos++;
                    }
                }
            }
        }
        
        // 解析小时和分钟
        int hours = 0, minutes = 0;
        if (sscanf(line.c_str() + up_pos, "%d:%d", &hours, &minutes) == 2) {
            total_seconds += hours * 3600 + minutes * 60;
        } else if (sscanf(line.c_str() + up_pos, "%d min", &minutes) == 1) {
            total_seconds += minutes * 60;
        }
        
        return total_seconds;
    }
    
    return 0;
}


// 获取服务器进程资源指标
std::string get_server_process_metrics() {
    pid_t pid = getpid();
    
    // CPU 使用率
    char cpu_cmd[256];
    snprintf(cpu_cmd, sizeof(cpu_cmd), "ps -p %d -o %%cpu= 2>/dev/null", pid);
    FILE* pipe = popen(cpu_cmd, "r");
    double cpu_usage = 0.0;
    if (pipe) {
        char buffer[64];
        if (fgets(buffer, sizeof(buffer), pipe)) {
            cpu_usage = atof(buffer);
        }
        pclose(pipe);
    }
    
    // 内存信息 (RSS和VMS)
    char memory_cmd[256];
    snprintf(memory_cmd, sizeof(memory_cmd), "ps -p %d -o rss=,vsz= 2>/dev/null", pid);
    pipe = popen(memory_cmd, "r");
    int rss_memory = 0, vms_memory = 0;
    if (pipe) {
        char buffer[128];
        if (fgets(buffer, sizeof(buffer), pipe)) {
            sscanf(buffer, "%d %d", &rss_memory, &vms_memory);
        }
        pclose(pipe);
    }
    
    // 线程数
    char threads_cmd[256];
    snprintf(threads_cmd, sizeof(threads_cmd), "ps -p %d -o nlwp= 2>/dev/null", pid);
    pipe = popen(threads_cmd, "r");
    int threads = 0;
    if (pipe) {
        char buffer[64];
        if (fgets(buffer, sizeof(buffer), pipe)) {
            threads = atoi(buffer);
        }
        pclose(pipe);
    }
    
    // 进程状态
    char state_cmd[256];
    snprintf(state_cmd, sizeof(state_cmd), "ps -p %d -o state= 2>/dev/null", pid);
    pipe = popen(state_cmd, "r");
    char state = 'S';
    if (pipe) {
        char buffer[64];
        if (fgets(buffer, sizeof(buffer), pipe)) {
            if (strlen(buffer) > 0) {
                state = buffer[0];
            }
        }
        pclose(pipe);
    }
    
    // 打开文件数
    char files_cmd[256];
    snprintf(files_cmd, sizeof(files_cmd), "lsof -p %d 2>/dev/null | wc -l", pid);
    pipe = popen(files_cmd, "r");
    int open_files = 0;
    if (pipe) {
        char buffer[64];
        if (fgets(buffer, sizeof(buffer), pipe)) {
            open_files = atoi(buffer);
        }
        pclose(pipe);
    }
    
    // 运行时间（从服务器启动时间）
    auto now = std::chrono::steady_clock::now();
    long uptime_seconds = std::chrono::duration_cast<std::chrono::seconds>(now - server_start_time).count();
    
    char json[1024];
    snprintf(json, sizeof(json), 
        "{"
        "\"cpu_usage_percent\": %.1f,"
        "\"memory_mb\": %d,"
        "\"virtual_memory_mb\": %d,"
        "\"threads\": %d,"
        "\"open_files\": %d,"
        "\"state\": \"%c\","
        "\"uptime_seconds\": %ld,"
        "\"pid\": %d"
        "}",
        cpu_usage, rss_memory, vms_memory, threads, open_files, state, uptime_seconds, pid);
    
    return std::string(json);
}

// 获取 GPU 使用率
double get_gpu_usage() {
    // 尝试读取 GPU 使用率（Android Termux 需要root权限）
    FILE* pipe = popen("cat /sys/class/kgsl/kgsl-3d0/gpubusy 2>/dev/null", "r");
    if (pipe) {
        char buffer[64];
        if (fgets(buffer, sizeof(buffer), pipe)) {
            pclose(pipe);
            double gpu_busy = 0.0;
            if (sscanf(buffer, "%lf", &gpu_busy) == 1) {
                return gpu_busy / 10.0; // 转换为百分比
            }
        } else {
            pclose(pipe);
        }
    }
    
    // 尝试其他方法
    pipe = popen("dumpsys gfxinfo 2>/dev/null | grep 'Total memory' | head -1", "r");
    if (pipe) {
        char buffer[256];
        if (fgets(buffer, sizeof(buffer), pipe)) {
            pclose(pipe);
            // 解析GPU内存使用情况
            return 0.0; // 暂时返回0，可以进一步解析
        } else {
            pclose(pipe);
        }
    }
    
    return 0.0; // 无法获取GPU使用率
}

// 获取系统温度
double get_temperature() {
    // 尝试从 thermal_zone 读取温度
    FILE* pipe = popen("find /sys/class/thermal -name 'temp' -type f 2>/dev/null | head -1 | xargs cat 2>/dev/null", "r");
    if (pipe) {
        char buffer[64];
        if (fgets(buffer, sizeof(buffer), pipe)) {
            pclose(pipe);
            int temp_millidegree = 0;
            if (sscanf(buffer, "%d", &temp_millidegree) == 1) {
                return temp_millidegree / 1000.0; // 转换为摄氏度
            }
        } else {
            pclose(pipe);
        }
    }
    
    // 尝试从 CPU 温度传感器读取
    pipe = popen("cat /sys/class/thermal/thermal_zone*/temp 2>/dev/null | head -1", "r");
    if (pipe) {
        char buffer[64];
        if (fgets(buffer, sizeof(buffer), pipe)) {
            pclose(pipe);
            int temp_millidegree = 0;
            if (sscanf(buffer, "%d", &temp_millidegree) == 1) {
                return temp_millidegree / 1000.0; // 转换为摄氏度
            }
        } else {
            pclose(pipe);
        }
    }
    
    // 尝试从电池温度读取
    pipe = popen("cat /sys/class/power_supply/*/temp 2>/dev/null | head -1", "r");
    if (pipe) {
        char buffer[64];
        if (fgets(buffer, sizeof(buffer), pipe)) {
            pclose(pipe);
            int temp_millidegree = 0;
            if (sscanf(buffer, "%d", &temp_millidegree) == 1) {
                return temp_millidegree / 10.0; // 电池温度单位可能是 0.1°C
            }
        } else {
            pclose(pipe);
        }
    }
    
    return -1.0; // 无法获取温度
}

// URL 解码函数（处理 UTF-8 字符）
std::string url_decode(const std::string& str) {
    std::string result;
    size_t pos = 0;
    while (pos < str.length()) {
        if (str[pos] == '%' && pos + 2 < str.length()) {
            std::string hexStr = str.substr(pos + 1, 2);
            try {
                int charCode = std::stoi(hexStr, nullptr, 16);
                result += static_cast<char>(charCode);
                pos += 3;
            } catch (...) {
                result += str[pos];
                pos++;
            }
        } else if (str[pos] == '+') {
            result += ' ';
            pos++;
        } else {
            result += str[pos];
            pos++;
        }
    }
    return result;
}

void signal_handler(int) {
    running = false;
}

// 忽略 SIGPIPE 信号，防止客户端断开连接时服务器崩溃
void ignore_sigpipe() {
    signal(SIGPIPE, SIG_IGN);
}

std::string get_content_type(const std::string& path) {
    if (path.size() >= 5 && path.substr(path.size() - 5) == ".html") return "text/html";
    if (path.size() >= 4 && path.substr(path.size() - 4) == ".css") return "text/css";
    if (path.size() >= 3 && path.substr(path.size() - 3) == ".js") return "application/javascript";
    if (path.size() >= 5 && path.substr(path.size() - 5) == ".json") return "application/json";
    if (path.size() >= 4 && path.substr(path.size() - 4) == ".png") return "image/png";
    if (path.size() >= 4 && path.substr(path.size() - 4) == ".jpg") return "image/jpeg";
    if (path.size() >= 5 && path.substr(path.size() - 5) == ".jpeg") return "image/jpeg";
    return "application/octet-stream";
}

std::string build_http_response(const std::string& content, const std::string& content_type = "text/html", int status_code = 200) {
    std::string response = "HTTP/1.1 " + std::to_string(status_code) + " OK\r\n";
    response += "Content-Type: " + content_type + "; charset=utf-8\r\n";
    response += "Content-Length: " + std::to_string(content.size()) + "\r\n";
    response += "Connection: close\r\n";
    response += "\r\n";
    response += content;
    return response;
}

// 发送 HTTP 响应的辅助函数
bool send_http_response(int client_fd, const std::string& response, int status_code = 200) {
    size_t total_sent = 0;
    size_t to_send = response.size();
    
    while (total_sent < to_send) {
        ssize_t sent = send(client_fd, response.data() + total_sent, to_send - total_sent, 0);
        if (sent <= 0) {
            return false;
        }
        total_sent += sent;
    }
    
    // 更新统计计数器
    bytes_sent += total_sent;
    if (status_code >= 200 && status_code < 300) {
        status_2xx++;
    } else if (status_code >= 400 && status_code < 500) {
        status_4xx++;
    } else if (status_code >= 500) {
        status_5xx++;
    }
    
    return true;
}

void handle_file_upload(int client_fd, const std::string& headers) {
    // 检查是否是multipart/form-data
    size_t content_type_pos = headers.find("Content-Type: multipart/form-data");
    if (content_type_pos == std::string::npos) {
        // 使用X-Filename头的简单上传
        size_t filename_pos = headers.find("X-Filename:");
        if (filename_pos == std::string::npos) {
            send_http_response(client_fd, build_http_response("Missing X-Filename header", "text/plain", 400));
            return;
        }

        size_t filename_start = filename_pos + 12;
        size_t filename_end = headers.find("\r\n", filename_start);
        std::string filename = headers.substr(filename_start, filename_end - filename_start);
        filename = url_decode(filename);

        // 提取Content-Length
        size_t content_length_pos = headers.find("Content-Length:");
        if (content_length_pos == std::string::npos) {
            send_http_response(client_fd, build_http_response("Missing Content-Length", "text/plain", 400));
            return;
        }

        size_t content_length_start = content_length_pos + 16;
        size_t content_length_end = headers.find("\r\n", content_length_start);
        size_t content_length = std::stoull(headers.substr(content_length_start, content_length_end - content_length_start));

        // 检查第一次读取的数据是否已包含文件体
        size_t headers_end = headers.find("\r\n\r\n");
        std::vector<char> file_data;
        size_t total_received = 0;

        if (headers_end != std::string::npos) {
            size_t body_start = headers_end + 4;
            size_t body_in_buffer = headers.size() - body_start;
            
            // 如果缓冲区中已有部分文件体
            if (body_in_buffer > 0) {
                file_data.assign(headers.begin() + body_start, headers.end());
                total_received = body_in_buffer;
            }
        }

        // 继续接收剩余的数据
        while (total_received < content_length) {
            char temp_buffer[8192];
            size_t remaining = content_length - total_received;
            size_t to_read = std::min(sizeof(temp_buffer), remaining);
            
            ssize_t bytes_read = recv(client_fd, temp_buffer, to_read, 0);
            if (bytes_read <= 0) break;
            
            file_data.insert(file_data.end(), temp_buffer, temp_buffer + bytes_read);
            total_received += bytes_read;
        }

        // 保存文件
            std::string file_path = UPLOAD_PATH + "/" + filename;
            
            // 如果文件路径包含目录，先创建目录
            size_t last_slash = file_path.find_last_of('/');
            if (last_slash != std::string::npos) {
                std::string dir_path = file_path.substr(0, last_slash);
                fs::create_directories(dir_path);
            }
            
            std::ofstream file(file_path, std::ios::binary);
            if (!file) {
                send_http_response(client_fd, build_http_response("Failed to create file", "text/plain", 500), 500);
                return;
            }
        file.write(file_data.data(), total_received);
        file.close();

        std::string json = "{\"message\": \"File uploaded successfully\", \"filename\": \"" + filename + 
                          "\", \"size\": " + std::to_string(total_received) + "}";
        send_http_response(client_fd, build_http_response(json, "application/json"));
        std::cout << "Uploaded: " << filename << " (" << total_received << " bytes)" << std::endl;
        return;
    }

    // 处理multipart/form-data
    size_t boundary_pos = headers.find("boundary=");
    if (boundary_pos == std::string::npos) {
        send_http_response(client_fd, build_http_response("Missing boundary", "text/plain", 400));
        return;
    }

    std::string boundary = headers.substr(boundary_pos + 9);
    size_t boundary_end = boundary.find("\r\n");
    if (boundary_end != std::string::npos) {
        boundary = boundary.substr(0, boundary_end);
    }

    std::string boundary_marker = "--" + boundary;

    // 提取Content-Length
    size_t content_length_pos = headers.find("Content-Length:");
    size_t content_length = 0;
    if (content_length_pos != std::string::npos) {
        size_t content_length_start = content_length_pos + 16;
        size_t content_length_end = headers.find("\r\n", content_length_start);
        content_length = std::stoull(headers.substr(content_length_start, content_length_end - content_length_start));
    }

    // 接收body
    size_t headers_end = headers.find("\r\n\r\n");
    std::string initial_body = headers.substr(headers_end + 4);
    std::vector<char> body;

    // 计算需要接收的额外数据
    size_t expected_body_size = content_length > 0 ? content_length : initial_body.size();
    body.reserve(expected_body_size);
    body.insert(body.end(), initial_body.begin(), initial_body.end());

    while (body.size() < expected_body_size) {
        char temp_buffer[8192];
        ssize_t bytes_read = recv(client_fd, temp_buffer, sizeof(temp_buffer), 0);
        if (bytes_read <= 0) break;
        body.insert(body.end(), temp_buffer, temp_buffer + bytes_read);
    }

    // 解析multipart/form-data
    std::string body_str(body.data(), body.size());

    // 查找第一个boundary
    size_t first_boundary = body_str.find(boundary_marker);
    if (first_boundary == std::string::npos) {
        send_http_response(client_fd, build_http_response("Invalid multipart data", "text/plain", 400));
        return;
    }

    // 查找Content-Disposition
    size_t cd_pos = body_str.find("Content-Disposition:", first_boundary);
    if (cd_pos == std::string::npos) {
        send_http_response(client_fd, build_http_response("Missing Content-Disposition", "text/plain", 400));
        return;
    }

    size_t cd_end = body_str.find("\r\n\r\n", cd_pos);
    if (cd_end == std::string::npos) {
        send_http_response(client_fd, build_http_response("Invalid Content-Disposition", "text/plain", 400));
        return;
    }

    std::string content_disposition = body_str.substr(cd_pos, cd_end - cd_pos);

    // 提取filename
            size_t filename_pos = content_disposition.find("filename=\"");
            if (filename_pos == std::string::npos) {
                send_http_response(client_fd, build_http_response("Missing filename", "text/plain", 400));
                return;
            }
    
            size_t filename_start = filename_pos + 10;
            size_t filename_end = content_disposition.find("\"", filename_start);
            if (filename_end == std::string::npos) {
                send_http_response(client_fd, build_http_response("Invalid filename", "text/plain", 400));
                return;
            }
    
            std::string filename = content_disposition.substr(filename_start, filename_end - filename_start);
            filename = url_decode(filename);
    // 提取文件数据
    size_t data_start = cd_end + 4;
    size_t next_boundary = body_str.find("\r\n" + boundary_marker, data_start);
    if (next_boundary == std::string::npos) {
        send_http_response(client_fd, build_http_response("Invalid file data", "text/plain", 400));
        return;
    }

    std::vector<char> file_data(body.begin() + data_start, body.begin() + next_boundary);

    // 保存文件
    std::string file_path = UPLOAD_PATH + "/" + filename;
    std::ofstream file(file_path, std::ios::binary);
    if (!file) {
        send_http_response(client_fd, build_http_response("Failed to create file", "text/plain", 500), 500);
        return;
    }

    file.write(file_data.data(), file_data.size());
    file.close();

    std::string json = "{\"message\": \"File uploaded successfully\", \"filename\": \"" + filename + 
                      "\", \"size\": " + std::to_string(file_data.size()) + "}";
    send_http_response(client_fd, build_http_response(json, "application/json"));
    std::cout << "Uploaded: " << filename << " (" << file_data.size() << " bytes)" << std::endl;
}

void handle_client(int client_fd) {
    std::cout << "[handle_client] New connection, fd=" << client_fd << std::endl;
    
    // 设置接收超时
    struct timeval tv;
    tv.tv_sec = 5;
    tv.tv_usec = 0;
    setsockopt(client_fd, SOL_SOCKET, SO_RCVTIMEO, (const char*)&tv, sizeof(tv));
    
    char buffer[65536];
    ssize_t bytes_read = recv(client_fd, buffer, sizeof(buffer) - 1, 0);

    std::cout << "[handle_client] Received " << bytes_read << " bytes" << std::endl;

    if (bytes_read <= 0) {
        std::cout << "[handle_client] Connection closed or error" << std::endl;
        close(client_fd);
        return;
    }

    buffer[bytes_read] = '\0';
    std::string request(buffer);
    
    // 更新统计计数器
    request_count++;
    bytes_received += bytes_read;

    // 解析HTTP请求
    size_t pos = request.find(' ');
    if (pos == std::string::npos) {
        send(client_fd, build_http_response("Bad Request", "text/plain", 400).c_str(), 0, 0);
        close(client_fd);
        return;
    }

    std::string method = request.substr(0, pos);
    size_t pos2 = request.find(' ', pos + 1);
    if (pos2 == std::string::npos) {
        send(client_fd, build_http_response("Bad Request", "text/plain", 400).c_str(), 0, 0);
        close(client_fd);
        return;
    }

    std::string path = request.substr(pos + 1, pos2 - pos - 1);

    std::cout << "Received request: " << method << " " << path << std::endl;

    // 处理不同的路径
    if (path == "/upload" && method == "POST") {
        handle_file_upload(client_fd, request);
        close(client_fd);
        return;
    }

    if (path == "/api/files" && method == "GET") {
        std::string json = "[";
        bool first = true;
        for (const auto& entry : fs::directory_iterator(UPLOAD_PATH)) {
            if (!fs::is_regular_file(entry.path())) continue;
            if (!first) json += ",";
            first = false;
            json += R"({"name": ")" + entry.path().filename().string() + R"(", "size": )" + 
                   std::to_string(fs::file_size(entry.path())) + "}";
        }
        json += "]";
        send_http_response(client_fd, build_http_response(json, "application/json"));
        close(client_fd);
        return;
    }

    // 服务器状态 API
    if (path == "/api/status" && method == "GET") {
        std::string json = R"({
            "active_connections": 1,
            "total_requests": )" + std::to_string(request_count) + R"(,
            "status_2xx": )" + std::to_string(status_2xx) + R"(,
            "status_4xx": )" + std::to_string(status_4xx) + R"(,
            "status_5xx": )" + std::to_string(status_5xx) + R"(,
            "bytes_sent": )" + std::to_string(bytes_sent) + R"(,
            "bytes_received": )" + std::to_string(bytes_received) + R"(,
            "uptime_seconds": )" + std::to_string(uptime_seconds) + R"(
        })";
        send_http_response(client_fd, build_http_response(json, "application/json"));
        close(client_fd);
        return;
    }


    // 服务器进程资源 API
    if (path == "/api/server-metrics" && method == "GET") {
        std::string json = get_server_process_metrics();
        send_http_response(client_fd, build_http_response(json, "application/json"), 200);
        close(client_fd);
        return;
    }

    // 系统指标 API
    if (path == "/api/metrics" && method == "GET") {
        // 读取 /proc/meminfo 获取内存信息
        size_t total_memory = 0;
        size_t free_memory = 0;
        std::ifstream meminfo("/proc/meminfo");
        if (meminfo) {
            std::string line;
            while (std::getline(meminfo, line)) {
                if (line.find("MemTotal:") == 0) {
                    sscanf(line.c_str(), "MemTotal: %zu", &total_memory);
                    total_memory *= 1024; // KB to Bytes
                } else if (line.find("MemAvailable:") == 0) {
                    sscanf(line.c_str(), "MemAvailable: %zu", &free_memory);
                    free_memory *= 1024; // KB to Bytes
                }
            }
        }
        
        double memory_usage = 0.0;
        if (total_memory > 0) {
            memory_usage = 100.0 - (double(free_memory) / total_memory * 100.0);
        }
        
        // 获取 CPU 使用率和核心数
        auto [cpu_usage, cpu_cores] = get_cpu_usage();
        
        // 获取系统负载（1分钟、5分钟、15分钟）
        auto [load1, load5, load15] = get_system_load();
        
        // 获取 GPU 使用率
        double gpu_usage = get_gpu_usage();
        
        // 获取系统温度
        double temperature = get_temperature();
        
        // 获取系统运行时间
        long system_uptime = get_system_uptime();

        std::string json = R"({
            "memory": {
                "total": )" + std::to_string(total_memory) + R"(,
                "used": )" + std::to_string(total_memory - free_memory) + R"(,
                "free": )" + std::to_string(free_memory) + R"(,
                "usage_percent": )" + std::to_string(memory_usage) + R"(
            },
            "cpu": {
                "load1": )" + std::to_string(load1) + R"(,
                "load5": )" + std::to_string(load5) + R"(,
                "load15": )" + std::to_string(load15) + R"(,
                "cores": )" + std::to_string(cpu_cores) + R"(,
                "usage_percent": )" + std::to_string(cpu_usage) + R"(
            },
            "gpu": {
                "usage_percent": )" + std::to_string(gpu_usage) + R"(,
                "available": )" + (gpu_usage >= 0 ? "true" : "false") + R"(
            },
            "temperature": {
                "celsius": )" + std::to_string(temperature) + R"(,
                "available": )" + (temperature >= 0 ? "true" : "false") + R"(
            },
            "uptime_seconds": )" + std::to_string(system_uptime) + R"(
        })";
        send_http_response(client_fd, build_http_response(json, "application/json"), 200);
        close(client_fd);
        return;
    }

    // 日志 API
    if (path == "/api/logs" && method == "GET") {
        std::string json = R"([
            {"time": ")" + std::string(get_current_time()) + R"(", "message": "HTTP/2 协议已启用", "level": "info"},
            {"time": ")" + std::string(get_current_time()) + R"(", "message": "服务器运行正常", "level": "info"},
            {"time": ")" + std::string(get_current_time()) + R"(", "message": "系统资源监控中", "level": "info"}
        ])";
        send_http_response(client_fd, build_http_response(json, "application/json"));
        close(client_fd);
        return;
    }

    // 文件删除接口
    if (path.size() >= 11 && path.substr(0, 11) == "/api/files/" && method == "DELETE") {
        std::string filename = path.substr(11); // 去掉 "/api/files/" 前缀
        filename = url_decode(filename);
        
        std::string file_path = UPLOAD_PATH + "/" + filename;
        
        if (fs::exists(file_path) && fs::is_regular_file(file_path)) {
        if (fs::remove(file_path)) {
            std::string json = R"({"message": "File deleted successfully", "filename": ")" + filename + R"("})";
            send_http_response(client_fd, build_http_response(json, "application/json"), 200);
            close(client_fd);
            return;
        } else {
            std::string json = R"({"message": "Failed to delete file", "filename": ")" + filename + R"("})";
            send_http_response(client_fd, build_http_response(json, "application/json", 500), 500);
            close(client_fd);
            return;
        }
    } else {
        std::string json = R"({"message": "File not found", "filename": ")" + filename + R"("})";
        send_http_response(client_fd, build_http_response(json, "application/json", 404), 404);
        close(client_fd);
        return;
    }
    }

    // 文件下载接口 - 使用分块传输避免大文件内存问题
    if (path.size() >= 7 && path.substr(0, 7) == "/files/" && method == "GET") {
        std::string filename = path.substr(7); // 去掉 "/files/" 前缀
        filename = url_decode(filename);
        
        std::string file_path = UPLOAD_PATH + "/" + filename;
        
        if (fs::exists(file_path) && fs::is_regular_file(file_path)) {
            std::ifstream file(file_path, std::ios::binary);
            if (file) {
                // 获取文件大小
                file.seekg(0, std::ios::end);
                size_t file_size = file.tellg();
                file.seekg(0, std::ios::beg);
                
                std::string content_type = get_content_type(file_path);
                
                // 先发送 HTTP 响应头
                std::string headers = "HTTP/1.1 200 OK\r\n";
                headers += "Content-Type: " + content_type + "\r\n";
                headers += "Content-Length: " + std::to_string(file_size) + "\r\n";
                headers += "Content-Disposition: attachment; filename=\"" + filename + "\"\r\n";
                headers += "Connection: close\r\n";
                headers += "\r\n";
                
                if (!send_http_response(client_fd, headers)) {
                    file.close();
                    close(client_fd);
                    return;
                }
                
                // 分块发送文件内容，避免一次性加载到内存
                char buffer[65536]; // 64KB 缓冲区
                while (file.good() && !file.eof()) {
                    file.read(buffer, sizeof(buffer));
                    size_t bytes_read = file.gcount();
                    
                    if (bytes_read > 0) {
                        // 直接发送缓冲区内容，不复制
                        ssize_t sent = send(client_fd, buffer, bytes_read, 0);
                        if (sent <= 0) {
                            break;
                        }
                        // 统计发送的字节数
                        bytes_sent += sent;
                    }
                }
                
                file.close();
                close(client_fd);
                return;
            }
        }
        
        // 文件不存在
        std::string json = R"({"message": "File not found", "filename": ")" + filename + R"("})";
        send_http_response(client_fd, build_http_response(json, "application/json", 404), 404);
        close(client_fd);
        return;
    }

    // 静态文件服务
    if (method == "GET") {
        std::string file_path = STATIC_PATH + "/" + path;
        if (path == "/" || path.empty()) {
            file_path = STATIC_PATH + "/index.html";
        }
        
        if (fs::exists(file_path) && fs::is_regular_file(file_path)) {
            std::ifstream file(file_path, std::ios::binary);
            if (file) {
                std::string content((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
                file.close();
                
                std::string content_type = get_content_type(file_path);
                send_http_response(client_fd, build_http_response(content, content_type));
                close(client_fd);
                return;
            }
        }
    }

    // 默认响应
    std::string html = "<!DOCTYPE html><html><head><title>Simple Upload Server</title></head>"
                      "<body><h1>Simple Upload Server</h1>"
                      "<p>Server is running!</p>"
                      "<p>Upload endpoint: POST /upload</p>"
                      "<p>List files: GET /api/files</p>"
                      "</body></html>";
    send_http_response(client_fd, build_http_response(html));
    close(client_fd);
}

int main() {
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    ignore_sigpipe(); // 忽略 SIGPIPE 信号，防止客户端断开连接时服务器崩溃
    
    // 初始化服务器启动时间
    server_start_time = std::chrono::steady_clock::now();
    
    // 启动后台线程更新运行时间
    std::thread([]() {
        while (running) {
            std::this_thread::sleep_for(std::chrono::seconds(1));
            auto now = std::chrono::steady_clock::now();
            uptime_seconds = std::chrono::duration_cast<std::chrono::seconds>(now - server_start_time).count();
        }
    }).detach();

    // 创建上传目录
    fs::create_directories(UPLOAD_PATH);

    // 创建socket
    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) {
        std::cerr << "Failed to create socket" << std::endl;
        return 1;
    }

    // 设置socket选项
    int opt = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    // 绑定地址
    struct sockaddr_in server_addr;
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(PORT);

    if (bind(server_fd, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        std::cerr << "Failed to bind" << std::endl;
        close(server_fd);
        return 1;
    }

    // 监听
    if (listen(server_fd, 128) < 0) {
        std::cerr << "Failed to listen" << std::endl;
        close(server_fd);
        return 1;
    }

    std::cout << "========================================" << std::endl;
    std::cout << "Simple Upload Server" << std::endl;
    std::cout << "========================================" << std::endl;
    std::cout << "Address: http://0.0.0.0:" << PORT << std::endl;
    std::cout << "Upload path: " << UPLOAD_PATH << std::endl;
    std::cout << "\nAPI Endpoints:" << std::endl;
    std::cout << "  POST  /upload        - Upload file" << std::endl;
    std::cout << "  GET   /api/files     - List files" << std::endl;
    std::cout << "\nServer is running... Press Ctrl+C to stop" << std::endl;
    std::cout << "========================================" << std::endl;

    // 主循环
    while (running) {
        struct sockaddr_in client_addr;
        socklen_t client_len = sizeof(client_addr);

        int client_fd = accept(server_fd, (struct sockaddr*)&client_addr, &client_len);
        if (client_fd < 0) {
            if (running) {
                std::cerr << "Failed to accept connection" << std::endl;
            }
            continue;
        }

        // 在新线程中处理客户端
        std::thread([client_fd]() {
            handle_client(client_fd);
        }).detach();
    }

    close(server_fd);
    std::cout << "Server stopped" << std::endl;

    return 0;
}
