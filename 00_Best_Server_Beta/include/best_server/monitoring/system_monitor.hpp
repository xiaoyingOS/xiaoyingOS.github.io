#pragma once

#include <string>
#include <cstdint>
#include <map>

namespace best_server {
namespace monitoring {

// 设备温度数据结构
struct DeviceTemperatures {
    double cpu;           // CPU温度
    double gpu;           // GPU温度
    double battery;       // 电池温度
    double memory;        // 内存温度
    double storage;       // 存储温度
    double display;       // 屏幕温度
    double wifi;          // WiFi温度
    double charger;       // 充电器温度
    double camera;        // 摄像头温度
    double video;         // 视频温度
    double body;          // 机身温度
    
    DeviceTemperatures()
        : cpu(0.0)
        , gpu(0.0)
        , battery(0.0)
        , memory(0.0)
        , storage(0.0)
        , display(0.0)
        , wifi(0.0)
        , charger(0.0)
        , camera(0.0)
        , video(0.0)
        , body(0.0)
    {}
};

// 系统监控数据结构
struct SystemMetrics {
    // CPU相关
    double cpu_usage_percent;
    double cpu_load1;
    double cpu_load5;
    double cpu_load15;
    int cpu_cores;
    double cpu_temperature; // 摄氏度
    
    // 内存相关
    uint64_t memory_total_mb;
    uint64_t memory_used_mb;
    uint64_t memory_free_mb;
    double memory_usage_percent;
    
    // 虚拟内存
    uint64_t virtual_memory_total_mb;
    uint64_t virtual_memory_used_mb;
    double virtual_memory_usage_percent;
    
    // 磁盘相关
    double disk_usage_percent;
    uint64_t disk_read_ios;    // 读IOPS（每秒读取次数）
    uint64_t disk_write_ios;   // 写IOPS（每秒写入次数）
    uint64_t disk_read_bytes_per_sec;   // 磁盘读取速度（字节/秒）
    uint64_t disk_write_bytes_per_sec;  // 磁盘写入速度（字节/秒）
    
    // GPU相关
    double gpu_usage_percent;
    double gpu_temperature; // 摄氏度
    uint64_t gpu_memory_used_mb;
    uint64_t gpu_memory_total_mb;
    double gpu_memory_usage_percent;
    
    // 网络相关
    uint64_t network_sent_bytes_per_sec;
    uint64_t network_received_bytes_per_sec;
    double network_latency_ms; // 毫秒
    
    // 进程相关
    int thread_count;
    int open_file_count;
    std::string process_state;
    uint64_t process_uptime_seconds;
    
    // 系统运行时间
    uint64_t system_uptime_seconds;
    
    // 设备温度
    DeviceTemperatures device_temps;
    
    SystemMetrics()
        : cpu_usage_percent(0.0)
        , cpu_load1(0.0)
        , cpu_load5(0.0)
        , cpu_load15(0.0)
        , cpu_cores(0)
        , cpu_temperature(0.0)
        , memory_total_mb(0)
        , memory_used_mb(0)
        , memory_free_mb(0)
        , memory_usage_percent(0.0)
        , virtual_memory_total_mb(0)
        , virtual_memory_used_mb(0)
        , virtual_memory_usage_percent(0.0)
        , disk_usage_percent(0.0)
        , disk_read_ios(0)
        , disk_write_ios(0)
        , disk_read_bytes_per_sec(0)
        , disk_write_bytes_per_sec(0)
        , gpu_usage_percent(0.0)
        , gpu_temperature(0.0)
        , gpu_memory_used_mb(0)
        , gpu_memory_total_mb(0)
        , gpu_memory_usage_percent(0.0)
        , network_sent_bytes_per_sec(0)
        , network_received_bytes_per_sec(0)
        , thread_count(0)
        , open_file_count(0)
        , process_state("")
        , process_uptime_seconds(0)
        , system_uptime_seconds(0)
        , device_temps()
    {}
};

// 获取CPU使用率
double get_cpu_usage();

// 获取CPU负载
double get_cpu_load1();
double get_cpu_load5();
double get_cpu_load15();

// 获取CPU温度
double get_cpu_temperature();

// 获取CPU核心数
int get_cpu_cores();

// 获取内存信息
void get_memory_info(uint64_t& total_mb, uint64_t& used_mb, uint64_t& free_mb);

// 获取内存使用率
double get_memory_usage_percent();

// 获取虚拟内存信息
void get_virtual_memory_info(uint64_t& total_mb, uint64_t& used_mb);

// 获取磁盘使用率
double get_disk_usage();

// 获取磁盘温度
double get_disk_temperature();

// 获取磁盘I/O使用率
double get_disk_io_usage();

// 获取磁盘I/O统计（字节数和IOPS）
void get_disk_io_stats(uint64_t& read_bytes_per_sec, uint64_t& write_bytes_per_sec,
                      uint64_t& read_ios, uint64_t& write_ios);

// 获取GPU信息
void get_gpu_info(double& usage_percent, double& temperature, 
                 uint64_t& memory_used_mb, uint64_t& memory_total_mb);

// 获取网络统计
void get_network_stats(uint64_t& sent_bytes_per_sec, uint64_t& received_bytes_per_sec);

// 获取网络延迟
double get_network_latency();

// 获取线程数
int get_thread_count();

// 获取打开的文件数
int get_open_file_count();

// 获取进程状态
std::string get_process_state();

// 获取进程运行时间
uint64_t get_process_uptime();

// 获取当前进程的内存使用（MB）
double get_process_memory();

// 获取当前进程的虚拟内存使用（GB）
double get_process_virtual_memory();

// 获取系统运行时间
uint64_t get_system_uptime();

// 获取设备温度
DeviceTemperatures get_device_temperatures();

// 获取所有系统指标
SystemMetrics get_all_system_metrics();

} // namespace monitoring
} // namespace best_server