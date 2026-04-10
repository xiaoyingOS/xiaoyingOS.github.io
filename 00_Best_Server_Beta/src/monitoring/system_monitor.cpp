#include "best_server/monitoring/system_monitor.hpp"

#include <fstream>
#include <sstream>
#include <algorithm>
#include <cstring>
#include <cstdlib>
#include <cinttypes>

#ifdef __linux__
#include <unistd.h>
#include <sys/sysinfo.h>
#include <sys/statvfs.h>
#include <dirent.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <ifaddrs.h>
#include <net/if.h>
#include <sys/ioctl.h>
#include <linux/netlink.h>
#include <linux/rtnetlink.h>
#include <sys/resource.h>
#include <proc/readproc.h>
#include <sys/types.h>
#include <signal.h>
#include <time.h>
#include <cmath>
#endif

#ifdef _WIN32
#include <windows.h>
#include <psapi.h>
#include <pdh.h>
#include <pdhmsg.h>
#include <tlhelp32.h>
#include <iphlpapi.h>
#include <winioctl.h>
#pragma comment(lib, "pdh.lib")
#pragma comment(lib, "iphlpapi.lib")
#pragma comment(lib, "psapi.lib")
#endif

#ifdef __APPLE__
#include <sys/types.h>
#include <sys/sysctl.h>
#include <sys/mount.h>
#include <mach/mach.h>
#include <mach/mach_host.h>
#include <mach/processor_info.h>
#include <ifaddrs.h>
#include <net/if.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/resource.h>
#include <libproc.h>
#endif

namespace best_server {
namespace monitoring {

#ifdef __linux__
// Linux平台实现

// CPU使用率相关
static unsigned long long prev_total = 0;
static unsigned long long prev_idle = 0;

// 温度传感器缓存
static int cached_cpu_temp_zone = -1;
static int cached_gpu_temp_zone = -1;
static int cached_disk_temp_zone = -1;
static bool thermal_zones_cached = false;

// 缓存温度传感器位置
void cache_thermal_zones() {
    if (thermal_zones_cached) return;
    
    for (int zone = 0; zone < 30; zone++) {
        std::string type_file = "/sys/class/thermal/thermal_zone" + std::to_string(zone) + "/type";
        std::ifstream type(type_file);
        if (type.is_open()) {
            std::string type_str;
            type >> type_str;
            
            if (cached_cpu_temp_zone == -1 && type_str.find("cpu") != std::string::npos) {
                cached_cpu_temp_zone = zone;
            } else if (cached_gpu_temp_zone == -1 && (type_str.find("gpu") != std::string::npos || type_str.find("gpuss") != std::string::npos)) {
                cached_gpu_temp_zone = zone;
            } else if (cached_disk_temp_zone == -1 && (type_str.find("storage") != std::string::npos || 
                       type_str.find("disk") != std::string::npos ||
                       type_str.find("emmc") != std::string::npos ||
                       type_str.find("ufs") != std::string::npos)) {
                cached_disk_temp_zone = zone;
            }
        }
    }
    
    thermal_zones_cached = true;
}

double get_cpu_usage() {
    std::ifstream file("/proc/stat");
    if (!file.is_open()) {
        return 0.0;
    }
    
    std::string line;
    std::getline(file, line);
    
    unsigned long long user, nice, system, idle, iowait, irq, softirq, steal;
    sscanf(line.c_str(), "cpu %llu %llu %llu %llu %llu %llu %llu %llu",
           &user, &nice, &system, &idle, &iowait, &irq, &softirq, &steal);

    unsigned long long total = user + nice + system + idle + iowait + irq + softirq + steal;

    if (prev_total == 0) {
        prev_total = total;
        prev_idle = idle;
        return 0.0;
    }
    
    unsigned long long total_diff = total - prev_total;
    unsigned long long idle_diff = idle - prev_idle;
    
    prev_total = total;
    prev_idle = idle;
    
    if (total_diff == 0) return 0.0;
    
    return ((double)(total_diff - idle_diff) / (double)total_diff) * 100.0;
}

double get_cpu_load1() {
    std::ifstream load_file("/proc/loadavg");
    if (load_file.is_open()) {
        double load1, load5, load15;
        load_file >> load1 >> load5 >> load15;
        return load1;
    }
    return 0.0;
}

double get_cpu_load5() {
    std::ifstream load_file("/proc/loadavg");
    if (load_file.is_open()) {
        double load1, load5, load15;
        load_file >> load1 >> load5 >> load15;
        return load5;
    }
    return 0.0;
}

double get_cpu_load15() {
    std::ifstream load_file("/proc/loadavg");
    if (load_file.is_open()) {
        double load1, load5, load15;
        load_file >> load1 >> load5 >> load15;
        return load15;
    }
    return 0.0;
}

int get_cpu_cores() {
    return sysconf(_SC_NPROCESSORS_ONLN);
}

double get_cpu_temperature() {
    // 确保缓存已初始化
    cache_thermal_zones();
    
    // 使用缓存的CPU温度传感器
    if (cached_cpu_temp_zone >= 0) {
        std::string temp_file = "/sys/class/thermal/thermal_zone" + std::to_string(cached_cpu_temp_zone) + "/temp";
        std::ifstream temp(temp_file);
        if (temp.is_open()) {
            int temp_value;
            temp >> temp_value;
            return temp_value / 1000.0; // 转换为摄氏度
        }
    }
    
    // 备用方案: 从 hwmon 获取温度
    std::ifstream temp_file("/sys/class/hwmon/hwmon0/temp1_input");
    if (temp_file.is_open()) {
        int temp;
        temp_file >> temp;
        return temp / 1000.0;
    }
    return 0.0;
}

void get_memory_info(uint64_t& total_mb, uint64_t& used_mb, uint64_t& free_mb) {
    std::ifstream meminfo("/proc/meminfo");
    if (!meminfo.is_open()) {
        return;
    }
    
    std::string line;
    unsigned long long mem_total = 0, mem_available = 0;
    
    while (std::getline(meminfo, line)) {
        if (line.find("MemTotal:") == 0) {
            sscanf(line.c_str(), "MemTotal: %llu kB", &mem_total);
        } else if (line.find("MemAvailable:") == 0) {
            sscanf(line.c_str(), "MemAvailable: %llu kB", &mem_available);
        }
    }
    
    if (mem_total == 0) return;
    
    unsigned long long mem_used = mem_total - mem_available;
    
    total_mb = mem_total / 1024;
    used_mb = mem_used / 1024;
    free_mb = mem_available / 1024;
}

double get_memory_usage_percent() {
    uint64_t total, used, free;
    get_memory_info(total, used, free);
    
    if (total == 0) return 0.0;
    
    return ((double)used / (double)total) * 100.0;
}

void get_virtual_memory_info(uint64_t& total_mb, uint64_t& used_mb) {
    struct sysinfo info;
    if (sysinfo(&info) == 0) {
        total_mb = info.totalswap / 1024;
        used_mb = (info.totalswap - info.freeswap) / 1024;
    } else {
        total_mb = 0;
        used_mb = 0;
    }
}

double get_disk_usage() {
    // 优先检查Termux数据目录（Android）
    const char* check_paths[] = {
        "/data/data/com.termux/files/",
        "/storage/emulated/0/",
        "/",
        NULL
    };
    
    for (int i = 0; check_paths[i] != NULL; i++) {
        struct statvfs stat;
        if (statvfs(check_paths[i], &stat) == 0) {
            unsigned long long total = stat.f_blocks * stat.f_frsize;
            unsigned long long free = stat.f_bavail * stat.f_frsize;
            unsigned long long used = total - free;
            
            if (total > 0 && total > 1024 * 1024) {  // 跳过太小的分区（如内存文件系统）
                return ((double)used / (double)total) * 100.0;
            }
        }
    }
    
    return 0.0;
}

double get_disk_temperature() {
    // 确保缓存已初始化
    cache_thermal_zones();
    
    // 使用缓存的磁盘温度传感器
    if (cached_disk_temp_zone >= 0) {
        std::string temp_file = "/sys/class/thermal/thermal_zone" + std::to_string(cached_disk_temp_zone) + "/temp";
        std::ifstream temp(temp_file);
        if (temp.is_open()) {
            int temp_value;
            temp >> temp_value;
            return temp_value / 1000.0; // 转换为摄氏度
        }
    }
    
    // 备用方案: 从 hwmon 获取温度
    std::ifstream temp_file("/sys/class/hwmon/hwmon1/temp1_input");
    if (temp_file.is_open()) {
        int temp;
        temp_file >> temp;
        return temp / 1000.0;
    }
    return 0.0;
}

double get_disk_io_usage() {
    // 从 /proc/diskstats 获取磁盘I/O
    static unsigned long long prev_read = 0;
    static unsigned long long prev_write = 0;
    static unsigned long long prev_time = 0;
    
    std::ifstream diskstats("/proc/diskstats");
    if (!diskstats.is_open()) {
        return 0.0;
    }
    
    std::string line;
    unsigned long long read_sectors = 0, write_sectors = 0;
    
    while (std::getline(diskstats, line)) {
        std::istringstream iss(line);
        std::string device;
        iss >> device >> read_sectors >> write_sectors;
        
        // 跳过loop设备和ram设备
        if (device.find("loop") != std::string::npos || 
            device.find("ram") != std::string::npos) {
            continue;
        }
        
        // 只获取主要设备
        break;
    }
    
    unsigned long long read_bytes = read_sectors * 512;
    unsigned long long write_bytes = write_sectors * 512;
    
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    unsigned long long current_time = ts.tv_sec * 1000000 + ts.tv_nsec / 1000;
    
    if (prev_time == 0) {
        prev_read = read_bytes;
        prev_write = write_bytes;
        prev_time = current_time;
        return 0.0;
    }
    
    unsigned long long time_diff = current_time - prev_time;
    if (time_diff == 0) return 0.0;
    
    unsigned long long read_diff = read_bytes - prev_read;
    unsigned long long write_diff = write_bytes - prev_write;
    unsigned long long total_diff = read_diff + write_diff;
    
    prev_read = read_bytes;
    prev_write = write_bytes;
    prev_time = current_time;
    
    // 估算I/O使用率（这是一个近似值）
    // 假设最大I/O速度为100MB/s
    double max_io_speed = 100.0 * 1024 * 1024; // 100MB/s
    double io_speed = (double)total_diff / ((double)time_diff / 1000000.0);
    
    return std::min(io_speed / max_io_speed * 100.0, 100.0);
}

void get_disk_io_stats(uint64_t& read_bytes_per_sec, uint64_t& write_bytes_per_sec,
                          uint64_t& read_ios, uint64_t& write_ios) {
    read_bytes_per_sec = 0;
    write_bytes_per_sec = 0;
    read_ios = 0;
    write_ios = 0;
    
    // 静态变量用于计算速度和IOPS
    static uint64_t prev_read_bytes = 0;
    static uint64_t prev_write_bytes = 0;
    static uint64_t prev_read_ios = 0;
    static uint64_t prev_write_ios = 0;
    static auto prev_time = std::chrono::steady_clock::now();
    static bool initialized = false;
    
    auto current_time = std::chrono::steady_clock::now();
    auto time_diff_ms = std::chrono::duration_cast<std::chrono::milliseconds>(current_time - prev_time).count();
    
    uint64_t current_read_bytes = 0;
    uint64_t current_write_bytes = 0;
    uint64_t current_read_ios = 0;
    uint64_t current_write_ios = 0;
    
    // 方法1：从 /proc/diskstats 获取系统级磁盘IOPS统计（更准确）
    std::ifstream diskstats_file("/proc/diskstats");
    if (diskstats_file.is_open()) {
        std::string line;
        while (std::getline(diskstats_file, line)) {
            std::istringstream iss(line);
            int major, minor;
            std::string device_name;
            uint64_t reads_completed, reads_merged, sectors_read, read_time_ms;
            uint64_t writes_completed, writes_merged, sectors_written, write_time_ms;
            uint64_t ios_in_progress, total_time_ms, weighted_time_ms;
            
            iss >> major >> minor >> device_name 
                >> reads_completed >> reads_merged >> sectors_read >> read_time_ms
                >> writes_completed >> writes_merged >> sectors_written >> write_time_ms
                >> ios_in_progress >> total_time_ms >> weighted_time_ms;
            
            // 跳过虚拟设备和分区，只统计主设备
            if (device_name.find("loop") != std::string::npos) continue;
            if (device_name.find("dm-") != std::string::npos) continue;
            if (device_name.find("mmcblk") != std::string::npos && device_name.find("p") != std::string::npos) continue;
            
            // 累加所有磁盘的I/O操作
            current_read_ios += reads_completed;
            current_write_ios += writes_completed;
            
            // 累加读写字节数（扇区数 * 512字节）
            current_read_bytes += sectors_read * 512;
            current_write_bytes += sectors_written * 512;
        }
    }
    
    // 方法2：如果无法获取磁盘统计，使用 /proc/self/io 作为后备
    if (current_read_ios == 0 && current_write_ios == 0) {
        std::ifstream io_file("/proc/self/io");
        if (io_file.is_open()) {
            std::string line;
            uint64_t rchar = 0, wchar = 0;
            uint64_t syscr_val = 0, syscw_val = 0;
            
            while (std::getline(io_file, line)) {
                if (line.find("read_bytes:") == 0) {
                    uint64_t bytes;
                    sscanf(line.c_str(), "read_bytes: %" SCNu64, &bytes);
                    current_read_bytes = bytes;
                } else if (line.find("write_bytes:") == 0) {
                    uint64_t bytes;
                    sscanf(line.c_str(), "write_bytes: %" SCNu64, &bytes);
                    current_write_bytes = bytes;
                } else if (line.find("rchar:") == 0) {
                    sscanf(line.c_str(), "rchar: %" SCNu64, &rchar);
                } else if (line.find("wchar:") == 0) {
                    sscanf(line.c_str(), "wchar: %" SCNu64, &wchar);
                } else if (line.find("syscr:") == 0) {
                    sscanf(line.c_str(), "syscr: %" SCNu64, &syscr_val);
                } else if (line.find("syscw:") == 0) {
                    sscanf(line.c_str(), "syscw: %" SCNu64, &syscw_val);
                }
            }
            
            // 如果read_bytes/write_bytes为0，使用rchar/wchar作为后备
            if (current_read_bytes == 0 && rchar > 0) {
                current_read_bytes = rchar;
            }
            if (current_write_bytes == 0 && wchar > 0) {
                current_write_bytes = wchar;
            }
            
            // 使用系统调用次数作为IOPS
            current_read_ios = syscr_val;
            current_write_ios = syscw_val;
        }
    }
    
    // 计算速度（字节/秒）和IOPS
    if (initialized && time_diff_ms > 0) {
        double time_diff_sec = time_diff_ms / 1000.0;
        
        if (current_read_bytes >= prev_read_bytes) {
            read_bytes_per_sec = (uint64_t)((current_read_bytes - prev_read_bytes) / time_diff_sec);
        }
        if (current_write_bytes >= prev_write_bytes) {
            write_bytes_per_sec = (uint64_t)((current_write_bytes - prev_write_bytes) / time_diff_sec);
        }
        if (current_read_ios >= prev_read_ios) {
            read_ios = (uint64_t)((current_read_ios - prev_read_ios) / time_diff_sec);
        }
        if (current_write_ios >= prev_write_ios) {
            write_ios = (uint64_t)((current_write_ios - prev_write_ios) / time_diff_sec);
        }
    }
    
    prev_read_bytes = current_read_bytes;
    prev_write_bytes = current_write_bytes;
    prev_read_ios = current_read_ios;
    prev_write_ios = current_write_ios;
    prev_time = current_time;
    initialized = true;
}

void get_gpu_info(double& usage_percent, double& temperature, 
                 uint64_t& memory_used_mb, uint64_t& memory_total_mb) {
    usage_percent = 0.0;
    temperature = 0.0;
    memory_used_mb = 0;
    memory_total_mb = 0;
    
    // 确保缓存已初始化
    cache_thermal_zones();
    
    // 使用缓存的GPU温度传感器
    if (cached_gpu_temp_zone >= 0) {
        std::string temp_file = "/sys/class/thermal/thermal_zone" + std::to_string(cached_gpu_temp_zone) + "/temp";
        std::ifstream temp(temp_file);
        if (temp.is_open()) {
            int temp_value;
            temp >> temp_value;
            temperature = temp_value / 1000.0;
        }
    }
}

void get_network_stats(uint64_t& sent_bytes_per_sec, uint64_t& received_bytes_per_sec) {
    sent_bytes_per_sec = 0;
    received_bytes_per_sec = 0;
    
    // 静态变量用于计算速度
    static uint64_t prev_sent = 0;
    static uint64_t prev_received = 0;
    static auto prev_time = std::chrono::steady_clock::now();
    static bool initialized = false;
    
    auto current_time = std::chrono::steady_clock::now();
    auto time_diff = std::chrono::duration_cast<std::chrono::seconds>(current_time - prev_time).count();
    
    uint64_t total_sent = 0;
    uint64_t total_received = 0;
    
    // 优先尝试读取 /proc/net/dev（系统级网络统计）
    std::ifstream net_file("/proc/net/dev");
    if (net_file.is_open()) {
        std::string line;
        // 跳过前两行标题
        std::getline(net_file, line);
        std::getline(net_file, line);
        
        while (std::getline(net_file, line)) {
            // 移除空白字符
            line.erase(0, line.find_first_not_of(" \t"));
            
            // 跳过回环接口
            if (line.find("lo:") == 0) continue;
            
            // 查找接收和发送字节数
            size_t colon_pos = line.find(':');
            if (colon_pos != std::string::npos) {
                std::string after_colon = line.substr(colon_pos + 1);
                std::istringstream iss(after_colon);
                
                unsigned long long rx_bytes, rx_packets, rx_errs, rx_drop, rx_fifo, rx_frame, rx_compressed, rx_multicast;
                unsigned long long tx_bytes, tx_packets, tx_errs, tx_drop, tx_fifo, tx_colls, tx_carrier, tx_compressed;
                
                if (iss >> rx_bytes >> rx_packets >> rx_errs >> rx_drop >> rx_fifo >> rx_frame >> rx_compressed >> rx_multicast
                       >> tx_bytes >> tx_packets >> tx_errs >> tx_drop >> tx_fifo >> tx_colls >> tx_carrier >> tx_compressed) {
                    total_received += rx_bytes;
                    total_sent += tx_bytes;
                }
            }
        }
    } else {
        // 备选方案：尝试读取 /proc/self/net/dev（进程级网络统计）
        std::ifstream self_net_file("/proc/self/net/dev");
        if (self_net_file.is_open()) {
            std::string line;
            // 跳过前两行标题
            std::getline(self_net_file, line);
            std::getline(self_net_file, line);
            
            while (std::getline(self_net_file, line)) {
                line.erase(0, line.find_first_not_of(" \t"));
                
                if (line.find("lo:") == 0) continue;
                
                size_t colon_pos = line.find(':');
                if (colon_pos != std::string::npos) {
                    std::string after_colon = line.substr(colon_pos + 1);
                    std::istringstream iss(after_colon);
                    
                    unsigned long long rx_bytes, tx_bytes;
                    iss >> rx_bytes >> tx_bytes >> tx_bytes >> tx_bytes >> tx_bytes >> tx_bytes >> tx_bytes >> tx_bytes
                       >> tx_bytes >> tx_bytes >> tx_bytes >> tx_bytes >> tx_bytes >> tx_bytes >> tx_bytes;
                    
                    total_received += rx_bytes;
                    total_sent += tx_bytes;
                }
            }
        }
    }
    
    // 计算速度（字节/秒）
    if (initialized && time_diff > 0) {
        if (total_sent >= prev_sent) {
            sent_bytes_per_sec = (total_sent - prev_sent) / time_diff;
        }
        if (total_received >= prev_received) {
            received_bytes_per_sec = (total_received - prev_received) / time_diff;
        }
    }
    
    prev_sent = total_sent;
    prev_received = total_received;
    prev_time = current_time;
    initialized = true;
}

double get_network_latency() {
    // 暂时禁用网络延迟测量，避免阻塞
    // TODO: 实现异步的网络延迟测量
    return 0.0;
}

int get_thread_count() {
    return sysconf(_SC_NPROCESSORS_ONLN);
}

int get_open_file_count() {
    DIR* proc_dir = opendir("/proc/self/fd");
    if (!proc_dir) {
        return 0;
    }
    
    int count = 0;
    struct dirent* entry;
    while ((entry = readdir(proc_dir)) != nullptr) {
        if (entry->d_type == DT_LNK) {
            count++;
        }
    }
    
    closedir(proc_dir);
    return count;
}

std::string get_process_state() {
    std::ifstream stat_file("/proc/self/stat");
    if (!stat_file.is_open()) {
        return "Unknown";
    }
    
    std::string line;
    std::getline(stat_file, line);
    
    std::istringstream iss(line);
    std::string skip;
    char state_code = 0;
    
    for (int i = 0; i < 2; ++i) iss >> skip;
    iss >> state_code;
    
    switch (state_code) {
        case 'R': return "Running";
        case 'S': return "Sleeping";
        case 'D': return "Disk sleep";
        case 'Z': return "Zombie";
        case 'T': return "Stopped";
        case 't': return "Tracing stop";
        case 'X': return "Dead";
        case 'x': return "Dead";
        case 'K': return "Wakekill";
        case 'W': return "Waking";
        case 'P': return "Parked";
        default: return "Unknown";
    }
}

uint64_t get_process_uptime() {
    struct sysinfo info;
    if (sysinfo(&info) == 0) {
        // 从 /proc/self/stat 获取进程启动时间
        std::ifstream stat_file("/proc/self/stat");
        if (stat_file.is_open()) {
            std::string line;
            std::getline(stat_file, line);
            
            std::istringstream iss(line);
            std::string skip;
            for (int i = 0; i < 21; ++i) iss >> skip;
            
            unsigned long long starttime;
            iss >> starttime;

            unsigned long long process_uptime = info.uptime - (starttime / sysconf(_SC_CLK_TCK));

            return process_uptime;
        }
    }
    return 0;
}

// 获取当前进程的内存使用（MB）
double get_process_memory() {
    std::ifstream status_file("/proc/self/status");
    if (!status_file.is_open()) {
        return 0.0;
    }
    
    std::string line;
    while (std::getline(status_file, line)) {
        if (line.find("VmRSS:") == 0) {
            std::istringstream iss(line);
            std::string label;
            double memory_kb;
            iss >> label >> memory_kb;
            return memory_kb / 1024.0; // 转换为MB
        }
    }
    return 0.0;
}

// 获取当前进程的虚拟内存使用（GB）
double get_process_virtual_memory() {
    std::ifstream status_file("/proc/self/status");
    if (!status_file.is_open()) {
        return 0.0;
    }
    
    std::string line;
    while (std::getline(status_file, line)) {
        if (line.find("VmSize:") == 0) {
            std::istringstream iss(line);
            std::string label;
            double memory_kb;
            iss >> label >> memory_kb;
            return memory_kb / 1024.0 / 1024.0; // 转换为GB
        }
    }
    return 0.0;
}

uint64_t get_system_uptime() {
    struct sysinfo info;
    if (sysinfo(&info) == 0) {
        return info.uptime;
    }
    return 0;
}

// 获取设备温度
DeviceTemperatures get_device_temperatures() {
    DeviceTemperatures temps;
    
    // CPU温度
    temps.cpu = get_cpu_temperature();
    
    // GPU温度 - 从get_gpu_info获取
    double gpu_usage, gpu_temp;
    uint64_t gpu_mem_used, gpu_mem_total;
    get_gpu_info(gpu_usage, gpu_temp, gpu_mem_used, gpu_mem_total);
    temps.gpu = gpu_temp;
    
    // 读取thermal_zone获取其他设备温度
    // 电池温度 - thermal_zone58
    std::ifstream battery_temp("/sys/class/thermal/thermal_zone58/temp");
    if (battery_temp.is_open()) {
        int temp_value;
        battery_temp >> temp_value;
        temps.battery = temp_value / 1000.0;
    }
    
    // 内存温度 - thermal_zone46 (ddr)
    std::ifstream memory_temp("/sys/class/thermal/thermal_zone46/temp");
    if (memory_temp.is_open()) {
        int temp_value;
        memory_temp >> temp_value;
        temps.memory = temp_value / 1000.0;
    }
    
    // 存储温度 - thermal_zone65 (flash_therm)
    std::ifstream storage_temp("/sys/class/thermal/thermal_zone65/temp");
    if (storage_temp.is_open()) {
        int temp_value;
        storage_temp >> temp_value;
        temps.storage = temp_value / 1000.0;
    }
    
    // 屏幕温度 - thermal_zone69 (display_therm)
    std::ifstream display_temp("/sys/class/thermal/thermal_zone69/temp");
    if (display_temp.is_open()) {
        int temp_value;
        display_temp >> temp_value;
        temps.display = temp_value / 1000.0;
    }
    
    // WiFi温度 - thermal_zone66 (wifi_therm)
    std::ifstream wifi_temp("/sys/class/thermal/thermal_zone66/temp");
    if (wifi_temp.is_open()) {
        int temp_value;
        wifi_temp >> temp_value;
        temps.wifi = temp_value / 1000.0;
    }
    
    // 充电器温度 - thermal_zone62 (charger_therm0)
    std::ifstream charger_temp("/sys/class/thermal/thermal_zone62/temp");
    if (charger_temp.is_open()) {
        int temp_value;
        charger_temp >> temp_value;
        temps.charger = temp_value / 1000.0;
    }
    
    // 摄像头温度 - thermal_zone35 (camera-0)
    std::ifstream camera_temp("/sys/class/thermal/thermal_zone35/temp");
    if (camera_temp.is_open()) {
        int temp_value;
        camera_temp >> temp_value;
        temps.camera = temp_value / 1000.0;
    }
    
    // 视频温度 - thermal_zone37 (video)
    std::ifstream video_temp("/sys/class/thermal/thermal_zone37/temp");
    if (video_temp.is_open()) {
        int temp_value;
        video_temp >> temp_value;
        temps.video = temp_value / 1000.0;
    }
    
    // 机身温度 - thermal_zone61 (conn_therm)
    std::ifstream body_temp("/sys/class/thermal/thermal_zone61/temp");
    if (body_temp.is_open()) {
        int temp_value;
        body_temp >> temp_value;
        temps.body = temp_value / 1000.0;
    }
    
    return temps;
}

#endif // __linux__

#ifdef _WIN32
// Windows平台实现

static PDH_HQUERY cpu_query = NULL;
static PDH_HCOUNTER cpu_counter = NULL;
static PDH_HQUERY disk_query = NULL;
static PDH_HCOUNTER disk_counter = NULL;
static bool pdh_initialized = false;

double get_cpu_usage() {
    if (!pdh_initialized) {
        PdhOpenQuery(NULL, 0, &cpu_query);
        PdhAddCounter(cpu_query, "\\Processor(_Total)\\% Processor Time", 0, &cpu_counter);
        PdhCollectQueryData(cpu_query);
        Sleep(1000);
        pdh_initialized = true;
    }
    
    PdhCollectQueryData(cpu_query);
    PDH_FMT_COUNTERVALUE counter_val;
    if (PdhGetFormattedCounterValue(cpu_counter, PDH_FMT_DOUBLE, NULL, &counter_val) == ERROR_SUCCESS) {
        return counter_val.doubleValue;
    }
    return 0.0;
}

double get_cpu_load1() {
    return get_cpu_usage();
}

double get_cpu_load5() {
    return get_cpu_usage();
}

double get_cpu_load15() {
    return get_cpu_usage();
}

int get_cpu_cores() {
    SYSTEM_INFO sys_info;
    GetSystemInfo(&sys_info);
    return sys_info.dwNumberOfProcessors;
}

double get_cpu_temperature() {
    // Windows下需要特定库支持
    return 0.0;
}

void get_memory_info(uint64_t& total_mb, uint64_t& used_mb, uint64_t& free_mb) {
    MEMORYSTATUSEX mem_status;
    mem_status.dwLength = sizeof(mem_status);
    GlobalMemoryStatusEx(&mem_status);
    
    total_mb = mem_status.ullTotalPhys / 1024 / 1024;
    free_mb = mem_status.ullAvailPhys / 1024 / 1024;
    used_mb = total_mb - free_mb;
}

double get_memory_usage_percent() {
    MEMORYSTATUSEX mem_status;
    mem_status.dwLength = sizeof(mem_status);
    GlobalMemoryStatusEx(&mem_status);
    return mem_status.dwMemoryLoad;
}

void get_virtual_memory_info(uint64_t& total_mb, uint64_t& used_mb) {
    MEMORYSTATUSEX mem_status;
    mem_status.dwLength = sizeof(mem_status);
    GlobalMemoryStatusEx(&mem_status);
    
    total_mb = mem_status.ullTotalPageFile / 1024 / 1024;
    used_mb = (mem_status.ullTotalPageFile - mem_status.ullAvailPageFile) / 1024 / 1024;
}

double get_disk_usage() {
    ULARGE_INTEGER free_bytes, total_bytes;
    if (GetDiskFreeSpaceEx("C:\\", &free_bytes, &total_bytes, NULL)) {
        if (total_bytes.QuadPart == 0) return 0.0;
        return ((double)(total_bytes.QuadPart - free_bytes.QuadPart) / (double)total_bytes.QuadPart) * 100.0;
    }
    return 0.0;
}

double get_disk_temperature() {
    return 0.0;
}

double get_disk_io_usage() {
    if (!pdh_initialized) {
        PdhOpenQuery(NULL, 0, &disk_query);
        PdhAddCounter(disk_query, "\\PhysicalDisk(_Total)\\% Disk Time", 0, &disk_counter);
        PdhCollectQueryData(disk_query);
        Sleep(1000);
    }
    
    PdhCollectQueryData(disk_query);
    PDH_FMT_COUNTERVALUE counter_val;
    if (PdhGetFormattedCounterValue(disk_counter, PDH_FMT_DOUBLE, NULL, &counter_val) == ERROR_SUCCESS) {
        return counter_val.doubleValue;
    }
    return 0.0;
}

void get_disk_io_stats(uint64_t& read_bytes_per_sec, uint64_t& write_bytes_per_sec) {
    read_bytes_per_sec = 0;
    write_bytes_per_sec = 0;
}

void get_gpu_info(double& usage_percent, double& temperature, 
                 uint64_t& memory_used_mb, uint64_t& memory_total_mb) {
    usage_percent = 0.0;
    temperature = 0.0;
    memory_used_mb = 0;
    memory_total_mb = 0;
}

void get_network_stats(uint64_t& sent_bytes_per_sec, uint64_t& received_bytes_per_sec) {
    sent_bytes_per_sec = 0;
    received_bytes_per_sec = 0;
}

double get_network_latency() {
    return 0.0;
}

int get_thread_count() {
    return get_cpu_cores();
}

int get_open_file_count() {
    return 0;
}

std::string get_process_state() {
    return "Running";
}

uint64_t get_process_uptime() {
    FILETIME creation_time, exit_time, kernel_time, user_time;
    if (GetProcessTimes(GetCurrentProcess(), &creation_time, &exit_time, &kernel_time, &user_time)) {
        FILETIME current_time;
        GetSystemTimeAsFileTime(&current_time);
        
        ULARGE_INTEGER creation, current;
        creation.LowPart = creation_time.dwLowDateTime;
        creation.HighPart = creation_time.dwHighDateTime;
        current.LowPart = current_time.dwLowDateTime;
        current.HighPart = current_time.dwHighDateTime;
        
        return (current.QuadPart - creation.QuadPart) / 10000000ULL;
    }
    return 0;
}

// 获取当前进程的内存使用（MB）
double get_process_memory() {
    PROCESS_MEMORY_COUNTERS_EX pmc;
    if (GetProcessMemoryInfo(GetCurrentProcess(), (PROCESS_MEMORY_COUNTERS*)&pmc, sizeof(pmc))) {
        return pmc.WorkingSetSize / 1024.0 / 1024.0; // 转换为MB
    }
    return 0.0;
}

// 获取当前进程的虚拟内存使用（GB）
double get_process_virtual_memory() {
    PROCESS_MEMORY_COUNTERS_EX pmc;
    if (GetProcessMemoryInfo(GetCurrentProcess(), (PROCESS_MEMORY_COUNTERS*)&pmc, sizeof(pmc))) {
        return pmc.PagefileUsage / 1024.0 / 1024.0 / 1024.0; // 转换为GB
    }
    return 0.0;
}

uint64_t get_system_uptime() {
    return GetTickCount64() / 1000;
}

// 获取设备温度（Windows平台）
DeviceTemperatures get_device_temperatures() {
    DeviceTemperatures temps;
    
    // Windows下可以尝试通过WMI获取温度信息
    // 但需要额外权限和配置，这里只返回基本温度
    temps.cpu = get_cpu_temperature();
    temps.gpu = get_gpu_temperature();
    
    // Windows下其他温度传感器需要特定驱动支持
    // 这里暂时返回0
    temps.battery = 0.0;
    temps.memory = 0.0;
    temps.storage = 0.0;
    temps.display = 0.0;
    temps.wifi = 0.0;
    temps.charger = 0.0;
    temps.camera = 0.0;
    temps.video = 0.0;
    temps.body = 0.0;
    
    return temps;
}

#endif // _WIN32__

#ifdef __APPLE__
// macOS平台实现

double get_cpu_usage() {
    host_cpu_load_info_data_t cpu_info;
    mach_msg_type_number_t count = HOST_CPU_LOAD_INFO_COUNT;
    
    if (host_statistics(mach_host_self(), HOST_CPU_LOAD_INFO, 
                        (host_info_t)&cpu_info, &count) == KERN_SUCCESS) {
        unsigned long long total = 0;
        for (int i = 0; i < CPU_STATE_MAX; i++) {
            total += cpu_info.cpu_ticks[i];
        }
        
        if (total == 0) return 0.0;
        return ((double)(cpu_info.cpu_ticks[CPU_STATE_USER] + 
                       cpu_info.cpu_ticks[CPU_STATE_SYSTEM] + 
                       cpu_info.cpu_ticks[CPU_STATE_NICE]) / 
                (double)total) * 100.0;
    }
    return 0.0;
}

double get_cpu_load1() {
    double load[3];
    if (getloadavg(load, 3) == 1) {
        return load[0];
    }
    return 0.0;
}

double get_cpu_load5() {
    double load[3];
    if (getloadavg(load, 3) == 1) {
        return load[1];
    }
    return 0.0;
}

double get_cpu_load15() {
    double load[3];
    if (getloadavg(load, 3) == 1) {
        return load[2];
    }
    return 0.0;
}

int get_cpu_cores() {
    int num_cores;
    size_t len = sizeof(num_cores);
    sysctlbyname("hw.ncpu", &num_cores, &len, NULL, 0);
    return num_cores;
}

double get_cpu_temperature() {
    return 0.0;
}

void get_memory_info(uint64_t& total_mb, uint64_t& used_mb, uint64_t& free_mb) {
    int mib[2];
    int64_t physical_memory;
    size_t length;
    
    mib[0] = CTL_HW;
    mib[1] = HW_MEMSIZE;
    length = sizeof(physical_memory);
    sysctl(mib, 2, &physical_memory, &length, NULL, 0);
    
    vm_statistics64_data_t vm_stats;
    mach_msg_type_number_t count = HOST_VM_INFO64_COUNT;
    
    if (host_statistics64(mach_host_self(), HOST_VM_INFO64, 
                         (host_info64_t)&vm_stats, &count) == KERN_SUCCESS) {
        uint64_t free = vm_stats.free_count * vm_page_size;
        uint64_t inactive = vm_stats.inactive_count * vm_page_size;
        uint64_t available = free + inactive;
        
        total_mb = physical_memory / 1024 / 1024;
        free_mb = available / 1024 / 1024;
        used_mb = total_mb - free_mb;
    }
}

double get_memory_usage_percent() {
    uint64_t total, used, free;
    get_memory_info(total, used, free);
    
    if (total == 0) return 0.0;
    return ((double)used / (double)total) * 100.0;
}

void get_virtual_memory_info(uint64_t& total_mb, uint64_t& used_mb) {
    struct xsw_usage swap_usage;
    size_t len = sizeof(swap_usage);
    
    if (sysctlbyname("vm.swapusage", &swap_usage, &len, NULL, 0) == 0) {
        total_mb = swap_usage.xsu_total / 1024 / 1024;
        used_mb = swap_usage.xsu_used / 1024 / 1024;
    } else {
        total_mb = 0;
        used_mb = 0;
    }
}

double get_disk_usage() {
    struct statfs stat;
    if (statfs("/", &stat) == 0) {
        uint64_t total = stat.f_blocks * stat.f_bsize;
        uint64_t free = stat.f_bavail * stat.f_bsize;
        
        if (total == 0) return 0.0;
        return ((double)(total - free) / (double)total) * 100.0;
    }
    return 0.0;
}

double get_disk_temperature() {
    return 0.0;
}

double get_disk_io_usage() {
    return 0.0;
}

void get_disk_io_stats(uint64_t& read_bytes_per_sec, uint64_t& write_bytes_per_sec) {
    read_bytes_per_sec = 0;
    write_bytes_per_sec = 0;
}

void get_gpu_info(double& usage_percent, double& temperature, 
                 uint64_t& memory_used_mb, uint64_t& memory_total_mb) {
    usage_percent = 0.0;
    temperature = 0.0;
    memory_used_mb = 0;
    memory_total_mb = 0;
}

void get_network_stats(uint64_t& sent_bytes_per_sec, uint64_t& received_bytes_per_sec) {
    sent_bytes_per_sec = 0;
    received_bytes_per_sec = 0;
}

double get_network_latency() {
    return 0.0;
}

int get_thread_count() {
    return get_cpu_cores();
}

int get_open_file_count() {
    return 0;
}

std::string get_process_state() {
    return "Running";
}

uint64_t get_process_uptime() {
    struct proc_bsdinfo proc_info;
    size_t len = sizeof(proc_info);
    
    if (proc_pidinfo(getpid(), PROC_PIDTBSDINFO, 0, 
                    &proc_info, len) > 0) {
        return proc_info.pbi_start_tvsec;
    }
    return 0;
}

// 获取当前进程的内存使用（MB）
double get_process_memory() {
    struct proc_taskinfo task_info;
    size_t len = sizeof(task_info);
    
    if (proc_pidinfo(getpid(), PROC_PIDTASKINFO, 0, 
                    &task_info, len) > 0) {
        return task_info.pti_resident_size / 1024.0 / 1024.0; // 转换为MB
    }
    return 0.0;
}

// 获取当前进程的虚拟内存使用（GB）
double get_process_virtual_memory() {
    struct proc_taskinfo task_info;
    size_t len = sizeof(task_info);
    
    if (proc_pidinfo(getpid(), PROC_PIDTASKINFO, 0, 
                    &task_info, len) > 0) {
        return task_info.pti_virtual_size / 1024.0 / 1024.0 / 1024.0; // 转换为GB
    }
    return 0.0;
}

uint64_t get_system_uptime() {
    struct timeval boot_time;
    size_t len = sizeof(boot_time);
    int mib[2] = {CTL_KERN, KERN_BOOTTIME};
    
    if (sysctl(mib, 2, &boot_time, &len, NULL, 0) == 0) {
        struct timeval current_time;
        gettimeofday(&current_time, NULL);
        
        return current_time.tv_sec - boot_time.tv_sec;
    }
    return 0;
}

// 获取设备温度（macOS平台）
DeviceTemperatures get_device_temperatures() {
    DeviceTemperatures temps;
    
    // macOS下可以尝试通过IOKit获取温度信息
    // 但需要额外权限和配置，这里只返回基本温度
    temps.cpu = get_cpu_temperature();
    temps.gpu = get_gpu_temperature();
    
    // macOS下其他温度传感器需要特定权限和工具
    // 这里暂时返回0
    temps.battery = 0.0;
    temps.memory = 0.0;
    temps.storage = 0.0;
    temps.display = 0.0;
    temps.wifi = 0.0;
    temps.charger = 0.0;
    temps.camera = 0.0;
    temps.video = 0.0;
    temps.body = 0.0;
    
    return temps;
}

#endif // __APPLE__

// 获取所有系统指标
SystemMetrics get_all_system_metrics() {
    SystemMetrics metrics;
    
    // CPU相关
    metrics.cpu_usage_percent = get_cpu_usage();
    metrics.cpu_load1 = get_cpu_load1();
    metrics.cpu_load5 = get_cpu_load5();
    metrics.cpu_load15 = get_cpu_load15();
    metrics.cpu_cores = get_cpu_cores();
    metrics.cpu_temperature = get_cpu_temperature();
    
    // 内存相关
    get_memory_info(metrics.memory_total_mb, metrics.memory_used_mb, metrics.memory_free_mb);
    metrics.memory_usage_percent = get_memory_usage_percent();
    
    // 虚拟内存
    get_virtual_memory_info(metrics.virtual_memory_total_mb, metrics.virtual_memory_used_mb);
    metrics.virtual_memory_usage_percent = 
        (metrics.virtual_memory_total_mb > 0) ? 
        ((double)metrics.virtual_memory_used_mb / (double)metrics.virtual_memory_total_mb) * 100.0 : 0.0;
    
    // 磁盘相关
    metrics.disk_usage_percent = get_disk_usage();
    get_disk_io_stats(metrics.disk_read_bytes_per_sec, metrics.disk_write_bytes_per_sec,
                      metrics.disk_read_ios, metrics.disk_write_ios);
    
    // GPU相关
    get_gpu_info(metrics.gpu_usage_percent, metrics.gpu_temperature,
                 metrics.gpu_memory_used_mb, metrics.gpu_memory_total_mb);
    metrics.gpu_memory_usage_percent = 
        (metrics.gpu_memory_total_mb > 0) ?
        ((double)metrics.gpu_memory_used_mb / (double)metrics.gpu_memory_total_mb) * 100.0 : 0.0;
    
    // 网络相关
    get_network_stats(metrics.network_sent_bytes_per_sec, metrics.network_received_bytes_per_sec);
    
    // 进程相关
    metrics.thread_count = get_thread_count();
    metrics.open_file_count = get_open_file_count();
    metrics.process_state = get_process_state();
    metrics.process_uptime_seconds = get_process_uptime();
    
    // 系统运行时间
    metrics.system_uptime_seconds = get_system_uptime();
    
    // 设备温度
    metrics.device_temps = get_device_temperatures();
    
    return metrics;
}

} // namespace monitoring
} // namespace best_server