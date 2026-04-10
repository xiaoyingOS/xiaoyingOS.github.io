#include <iostream>
#include <fstream>
#include <string>
#include <vector>
#include <cstring>
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <ctime>

#ifdef _WIN32
#include <windows.h>
#include <psapi.h>
#include <pdh.h>
#include <pdhmsg.h>
#include <tlhelp32.h>
#include <iphlpapi.h>
#pragma comment(lib, "pdh.lib")
#pragma comment(lib, "iphlpapi.lib")

// 可选：如果已集成NVML（NVIDIA GPU），取消注释
#define USE_NVML
#include <nvml.h>
#pragma comment(lib, "nvml.lib")

// 可选：如果已集成ADL（AMD GPU），取消注释
// #define USE_ADL
// #include "adl_sdk.h"
// #pragma comment(lib, "atiadlxx.lib")
#endif

// 获取CPU使用率
double getCpuUsage() {
#ifdef __linux__
    std::ifstream file("/proc/stat");
    if (!file.is_open()) {
        std::cerr << "无法打开 /proc/stat" << std::endl;
        return -1.0;
    }
    
    std::string line;
    std::getline(file, line);
    
    long user, nice, system, idle, iowait, irq, softirq, steal;
    sscanf(line.c_str(), "cpu %ld %ld %ld %ld %ld %ld %ld %ld",
           &user, &nice, &system, &idle, &iowait, &irq, &softirq, &steal);
    
    long total = user + nice + system + idle + iowait + irq + softirq + steal;
    if (total == 0) return 0.0;
    
    double usage = ((double)(user + nice + system) / (double)total) * 100.0;
    std::cout << "✅ CPU使用率: " << usage << "%" << std::endl;
    return usage;
#elif _WIN32
    // Windows平台使用PDH获取CPU使用率
    static PDH_HQUERY cpuQuery = NULL;
    static PDH_HCOUNTER cpuCounter = NULL;
    static bool initialized = false;
    
    if (!initialized) {
        PdhOpenQuery(NULL, 0, &cpuQuery);
        PdhAddCounter(cpuQuery, "\\Processor(_Total)\\% Processor Time", 0, &cpuCounter);
        PdhCollectQueryData(cpuQuery);
        Sleep(1000); // 等待一秒获取差值
        initialized = true;
    }
    
    PdhCollectQueryData(cpuQuery);
    PDH_FMT_COUNTERVALUE counterVal;
    PdhGetFormattedCounterValue(cpuCounter, PDH_FMT_DOUBLE, NULL, &counterVal);
    
    double usage = counterVal.doubleValue;
    std::cout << "✅ CPU使用率: " << usage << "%" << std::endl;
    return usage;
#else
    std::cout << "❌ CPU使用率: 平台不支持" << std::endl;
    return -1.0;
#endif
}

// 获取内存使用
struct MemoryInfo {
    unsigned long total;
    unsigned long available;
    unsigned long used;
};

MemoryInfo getMemoryInfo() {
#ifdef __linux__
    MemoryInfo info = {0, 0, 0};
    
    std::ifstream meminfo("/proc/meminfo");
    if (!meminfo.is_open()) {
        std::cerr << "无法打开 /proc/meminfo" << std::endl;
        return info;
    }
    
    std::string line;
    while (std::getline(meminfo, line)) {
        if (line.find("MemTotal:") == 0) {
            sscanf(line.c_str(), "MemTotal: %lu kB", &info.total);
        } else if (line.find("MemAvailable:") == 0) {
            sscanf(line.c_str(), "MemAvailable: %lu kB", &info.available);
        }
    }
    
    info.used = info.total - info.available;
    double usage = ((double)info.used / (double)info.total) * 100.0;
    
    std::cout << "✅ 内存使用: " << info.used << " KB / " << info.total << " KB (" << usage << "%)" << std::endl;
    std::cout << "✅ 可用内存: " << info.available << " KB" << std::endl;
    return info;
#elif _WIN32
    MemoryInfo info = {0, 0, 0};
    MEMORYSTATUSEX status;
    status.dwLength = sizeof(status);
    GlobalMemoryStatusEx(&status);
    
    info.total = status.ullTotalPhys / 1024; // 转换为KB
    info.available = status.ullAvailPhys / 1024;
    info.used = info.total - info.available;
    
    double usage = status.dwMemoryLoad;
    std::cout << "✅ 内存使用: " << info.used << " KB / " << info.total << " KB (" << usage << "%)" << std::endl;
    std::cout << "✅ 可用内存: " << info.available << " KB" << std::endl;
    return info;
#else
    std::cout << "❌ 内存使用: 平台不支持" << std::endl;
    MemoryInfo info = {0, 0, 0};
    return info;
#endif
}

// 获取系统内存占用（系统总内存使用情况）
void getSystemMemoryUsage() {
#ifdef __linux__
    std::ifstream meminfo("/proc/meminfo");
    if (!meminfo.is_open()) {
        std::cerr << "无法打开 /proc/meminfo" << std::endl;
        return;
    }
    
    unsigned long mem_total = 0, mem_free = 0, mem_available = 0, buffers = 0, cached = 0;
    
    std::string line;
    while (std::getline(meminfo, line)) {
        if (line.find("MemTotal:") == 0) {
            sscanf(line.c_str(), "MemTotal: %lu kB", &mem_total);
        } else if (line.find("MemFree:") == 0) {
            sscanf(line.c_str(), "MemFree: %lu kB", &mem_free);
        } else if (line.find("MemAvailable:") == 0) {
            sscanf(line.c_str(), "MemAvailable: %lu kB", &mem_available);
        } else if (line.find("Buffers:") == 0) {
            sscanf(line.c_str(), "Buffers: %lu kB", &buffers);
        } else if (line.find("Cached:") == 0) {
            sscanf(line.c_str(), "Cached: %lu kB", &cached);
        }
    }
    
    unsigned long mem_used = mem_total - mem_free - buffers - cached;
    double usage = ((double)mem_used / (double)mem_total) * 100.0;
    
    std::cout << "✅ 系统内存占用:" << std::endl;
    std::cout << "   总内存: " << (mem_total / 1024 / 1024) << " GB" << std::endl;
    std::cout << "   已使用: " << (mem_used / 1024 / 1024) << " GB (" << usage << "%)" << std::endl;
    std::cout << "   可用: " << (mem_available / 1024 / 1024) << " GB" << std::endl;
    std::cout << "   缓存: " << (cached / 1024 / 1024) << " GB" << std::endl;
    std::cout << "   缓冲: " << (buffers / 1024 / 1024) << " GB" << std::endl;
#elif _WIN32
    // Windows 的 PERFORMANCE_INFORMATION 结构提供更详细的内存信息
    PERFORMANCE_INFORMATION perfInfo;
    perfInfo.cb = sizeof(PERFORMANCE_INFORMATION);
    
    // 获取性能计数器
    if (GetPerformanceInfo(&perfInfo, sizeof(perfInfo))) {
        unsigned long long page_size = perfInfo.PageSize;
        unsigned long long mem_total = (perfInfo.PhysicalTotal * page_size) / 1024 / 1024 / 1024; // GB
        unsigned long long mem_available = (perfInfo.PhysicalAvailable * page_size) / 1024 / 1024 / 1024;
        unsigned long long mem_used = mem_total - mem_available;
        unsigned long long system_cache = (perfInfo.SystemCache * page_size) / 1024 / 1024 / 1024;
        unsigned long long kernel_nonpaged = (perfInfo.KernelNonpaged * page_size) / 1024 / 1024 / 1024;
        unsigned long long kernel_paged = (perfInfo.KernelPaged * page_size) / 1024 / 1024 / 1024;
        
        double usage = ((double)mem_used / (double)mem_total) * 100.0;
        
        std::cout << "✅ 系统内存占用:" << std::endl;
        std::cout << "   总内存: " << mem_total << " GB" << std::endl;
        std::cout << "   已使用: " << mem_used << " GB (" << usage << "%)" << std::endl;
        std::cout << "   可用: " << mem_available << " GB" << std::endl;
        std::cout << "   系统缓存: " << system_cache << " GB" << std::endl;
        std::cout << "   内核非分页: " << kernel_nonpaged << " GB" << std::endl;
        std::cout << "   内核分页: " << kernel_paged << " GB" << std::endl;
        
        // 计算应用程序实际使用的内存（排除系统缓存）
        unsigned long long app_memory = mem_used - system_cache - kernel_nonpaged - kernel_paged;
        if (app_memory < 0) app_memory = 0;
        std::cout << "   应用程序内存: " << app_memory << " GB" << std::endl;
    } else {
        // 备用方案：使用 MEMORYSTATUSEX
        MEMORYSTATUSEX status;
        status.dwLength = sizeof(status);
        GlobalMemoryStatusEx(&status);
        
        unsigned long mem_total = status.ullTotalPhys / 1024 / 1024 / 1024; // GB
        unsigned long mem_available = status.ullAvailPhys / 1024 / 1024 / 1024;
        unsigned long mem_used = mem_total - mem_available;
        double usage = status.dwMemoryLoad;
        
        std::cout << "✅ 系统内存占用:" << std::endl;
        std::cout << "   总内存: " << mem_total << " GB" << std::endl;
        std::cout << "   已使用: " << mem_used << " GB (" << usage << "%)" << std::endl;
        std::cout << "   可用: " << mem_available << " GB" << std::endl;
        std::cout << "   系统缓存: N/A (需要 GetPerformanceInfo)" << std::endl;
        std::cout << "   内核内存: N/A (需要 GetPerformanceInfo)" << std::endl;
    }
#else
    std::cout << "❌ 系统内存占用: 平台不支持" << std::endl;
#endif
}

// 获取虚拟内存
void getVirtualMemoryInfo() {
#ifdef __linux__
    std::ifstream meminfo("/proc/meminfo");
    if (!meminfo.is_open()) {
        std::cerr << "无法打开 /proc/meminfo" << std::endl;
        return;
    }
    
    unsigned long vmsize = 0;
    unsigned long vmrss = 0;
    unsigned long swap_total = 0;
    unsigned long swap_free = 0;
    
    std::string line;
    while (std::getline(meminfo, line)) {
        if (line.find("SwapTotal:") == 0) {
            sscanf(line.c_str(), "SwapTotal: %lu kB", &swap_total);
        } else if (line.find("SwapFree:") == 0) {
            sscanf(line.c_str(), "SwapFree: %lu kB", &swap_free);
        }
    }
    
    // 从 /proc/self/status 获取进程的虚拟内存
    std::ifstream status("/proc/self/status");
    while (std::getline(status, line)) {
        if (line.find("VmSize:") == 0) {
            sscanf(line.c_str(), "VmSize: %lu kB", &vmsize);
        } else if (line.find("VmRSS:") == 0) {
            sscanf(line.c_str(), "VmRSS: %lu kB", &vmrss);
        }
    }
    
    std::cout << "✅ 虚拟内存(进程): " << vmsize << " KB" << std::endl;
    std::cout << "✅ 物理内存(进程): " << vmrss << " KB" << std::endl;
    std::cout << "✅ 交换空间: " << swap_free << " KB / " << swap_total << " KB" << std::endl;
#elif _WIN32
    MEMORYSTATUSEX status;
    status.dwLength = sizeof(status);
    GlobalMemoryStatusEx(&status);
    
    unsigned long swap_total = status.ullTotalPageFile / 1024;
    unsigned long swap_free = status.ullAvailPageFile / 1024;
    unsigned long swap_used = swap_total - swap_free;
    
    PROCESS_MEMORY_COUNTERS_EX pmc;
    GetProcessMemoryInfo(GetCurrentProcess(), (PROCESS_MEMORY_COUNTERS*)&pmc, sizeof(pmc));
    
    std::cout << "✅ 虚拟内存(进程): " << (pmc.PrivateUsage / 1024) << " KB" << std::endl;
    std::cout << "✅ 物理内存(进程): " << (pmc.WorkingSetSize / 1024) << " KB" << std::endl;
    std::cout << "✅ 交换空间: " << swap_free << " KB / " << swap_total << " KB" << std::endl;
#else
    std::cout << "❌ 虚拟内存: 平台不支持" << std::endl;
#endif
}

// 获取线程数
int getThreadCount() {
#ifdef __linux__
    std::ifstream file("/proc/self/status");
    if (!file.is_open()) {
        std::cerr << "无法打开 /proc/self/status" << std::endl;
        return -1;
    }
    
    std::string line;
    int threads = 0;
    while (std::getline(file, line)) {
        if (line.find("Threads:") == 0) {
            sscanf(line.c_str(), "Threads: %d", &threads);
            break;
        }
    }
    
    std::cout << "✅ 线程数: " << threads << std::endl;
    return threads;
#elif _WIN32
    DWORD threadCount = 0;
    HANDLE hThreadSnap = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
    if (hThreadSnap != INVALID_HANDLE_VALUE) {
        THREADENTRY32 te32;
        te32.dwSize = sizeof(THREADENTRY32);
        DWORD processId = GetCurrentProcessId();
        
        if (Thread32First(hThreadSnap, &te32)) {
            do {
                if (te32.th32OwnerProcessID == processId) {
                    threadCount++;
                }
            } while (Thread32Next(hThreadSnap, &te32));
        }
        CloseHandle(hThreadSnap);
    }
    
    std::cout << "✅ 线程数: " << threadCount << std::endl;
    return threadCount;
#else
    std::cout << "❌ 线程数: 平台不支持" << std::endl;
    return -1;
#endif
}

// 获取进程状态
std::string getProcessState() {
#ifdef __linux__
    std::ifstream file("/proc/self/status");
    if (!file.is_open()) {
        std::cerr << "无法打开 /proc/self/status" << std::endl;
        return "Unknown";
    }
    
    std::string line;
    std::string state = "Unknown";
    while (std::getline(file, line)) {
        if (line.find("State:") == 0) {
            // State: R (running)
            size_t pos = line.find('(');
            size_t end_pos = line.find(')');
            if (pos != std::string::npos && end_pos != std::string::npos) {
                state = line.substr(pos + 1, end_pos - pos - 1);
            }
            break;
        }
    }
    
    std::cout << "✅ 进程状态: " << state << std::endl;
    return state;
#elif _WIN32
    std::string state = "running";
    DWORD exitCode;
    if (GetExitCodeProcess(GetCurrentProcess(), &exitCode)) {
        if (exitCode == STILL_ACTIVE) {
            state = "running";
        } else {
            state = "terminated";
        }
    }
    
    std::cout << "✅ 进程状态: " << state << std::endl;
    return state;
#else
    std::cout << "❌ 进程状态: 平台不支持" << std::endl;
    return "Unknown";
#endif
}

// 获取打开的文件数
int getOpenFileCount() {
#ifdef __linux__
    std::string path = "/proc/self/fd";
    int count = 0;
    
    std::ifstream file(path + "/../" + "status");
    if (file.is_open()) {
        std::string line;
        while (std::getline(file, line)) {
            if (line.find("FdSize:") == 0) {
                // 从 /proc/self/fd 目录数
                // 简单方法：数 /proc/self/fd 目录中的文件数
                break;
            }
        }
    }
    
    // 统计 /proc/self/fd 中的文件数
    std::string cmd = "ls -1 /proc/self/fd 2>/dev/null | wc -l";
    FILE* pipe = popen(cmd.c_str(), "r");
    if (pipe) {
        char buffer[32];
        if (fgets(buffer, sizeof(buffer), pipe)) {
            count = atoi(buffer);
        }
        pclose(pipe);
    }
    
    std::cout << "✅ 打开的文件数: " << count << std::endl;
    return count;
#elif _WIN32
    // Windows平台使用GetProcessHandleCount获取句柄数
    DWORD handleCount = 0;
    if (GetProcessHandleCount(GetCurrentProcess(), &handleCount)) {
        std::cout << "✅ 打开的句柄数: " << handleCount << std::endl;
        return handleCount;
    }
    std::cout << "❌ 打开的句柄数: 无法获取" << std::endl;
    return -1;
#else
    std::cout << "❌ 打开的文件数: 平台不支持" << std::endl;
    return -1;
#endif
}

// 获取进程运行时间
long getProcessUptime() {
#ifdef __linux__
    std::ifstream file("/proc/self/stat");
    if (!file.is_open()) {
        std::cerr << "无法打开 /proc/self/stat" << std::endl;
        return -1;
    }
    
    std::string line;
    std::getline(file, line);
    
    // 获取第22个字段（starttime）
    std::vector<std::string> fields;
    size_t pos = 0;
    std::string field;
    for (int i = 0; i < 22; i++) {
        pos = line.find(' ', pos);
        if (pos == std::string::npos) break;
        pos++;
    }
    
    size_t end_pos = line.find(' ', pos);
    if (end_pos != std::string::npos) {
        field = line.substr(pos, end_pos - pos);
        long starttime = std::stol(field);
        long uptime = starttime;
        
        long seconds = uptime / 100;
        long minutes = seconds / 60;
        long hours = minutes / 60;
        minutes %= 60;
        seconds %= 60 * 60;
        
        std::cout << "✅ 运行时间: " << hours << "小时 " << minutes << "分钟 " << seconds << "秒" << std::endl;
        return uptime;
    }
    
    return -1;
#elif _WIN32
    FILETIME creationTime, exitTime, kernelTime, userTime;
    if (GetProcessTimes(GetCurrentProcess(), &creationTime, &exitTime, &kernelTime, &userTime)) {
        ULARGE_INTEGER uli;
        uli.LowPart = creationTime.dwLowDateTime;
        uli.HighPart = creationTime.dwHighDateTime;
        
        // Windows文件时间是100纳秒间隔，需要转换
        long long creationTimeMs = uli.QuadPart / 10000;
        
        // 获取系统启动时间
        FILETIME systemTimeAsFileTime;
        GetSystemTimeAsFileTime(&systemTimeAsFileTime);
        uli.LowPart = systemTimeAsFileTime.dwLowDateTime;
        uli.HighPart = systemTimeAsFileTime.dwHighDateTime;
        long long currentTimeMs = uli.QuadPart / 10000;
        
        long long uptimeMs = currentTimeMs - creationTimeMs;
        long uptimeSec = uptimeMs / 1000;
        
        long seconds = uptimeSec % 60;
        long minutes = (uptimeSec / 60) % 60;
        long hours = uptimeSec / 3600;
        
        std::cout << "✅ 运行时间: " << hours << "小时 " << minutes << "分钟 " << seconds << "秒" << std::endl;
        return uptimeSec;
    }
    
    std::cout << "❌ 运行时间: 无法获取" << std::endl;
    return -1;
#else
    std::cout << "❌ 运行时间: 平台不支持" << std::endl;
    return -1;
#endif
}

// 获取温度信息
double getTemperature() {
#ifdef __linux__
    // 从 /sys/class/thermal/thermal_zone*/temp 读取温度
    double temp = -1.0;
    int zone_count = 0;
    
    for (int i = 0; i < 10; i++) {
        std::string temp_path = "/sys/class/thermal/thermal_zone" + std::to_string(i) + "/temp";
        std::ifstream file(temp_path);
        if (file.is_open()) {
            int temp_raw;
            file >> temp_raw;
            // 温度单位是毫摄氏度，需要除以1000
            double zone_temp = temp_raw / 1000.0;
            if (zone_temp > 0 && zone_temp < 100) {  // 合理的温度范围
                temp = zone_temp;
                zone_count++;
                break;  // 只读取第一个有效的温度传感器
            }
        }
    }
    
    if (temp > 0) {
        std::cout << "✅ 设备温度: " << temp << "°C" << std::endl;
    } else {
        std::cout << "❌ 设备温度: 无法获取" << std::endl;
    }
    
    return temp;
#elif _WIN32
    double temp = -1.0;

#ifdef USE_LIBRE_HARDWARE_MONITOR
    // 方案1: 使用LibreHardwareMonitor（推荐，最准确）
    // 需要先集成LibreHardwareMonitor库
    // LHM::Update();
    // temp = LHM::GetCpuTemperature();
    // if (temp > 0) {
    //     std::cout << "✅ 设备温度 (LibreHardwareMonitor): " << temp << "°C" << std::endl;
    //     return temp;
    // }
#endif

    // #CPU温度获取: Windows 10/11 已弃用 ACPI 温度接口，无法通过 WMI 获取
    // 建议使用第三方工具：
    // - HWiNFO64: https://www.hwinfo.com/
    // - LibreHardwareMonitor: https://github.com/LibreHardwareMonitor/LibreHardwareMonitor
    // - Open Hardware Monitor: https://openhardwaremonitor.org/
    
    std::cout << "⚠️ 设备温度: Windows 10/11 已弃用 ACPI 温度接口" << std::endl;
    std::cout << "   建议使用第三方工具获取温度信息" << std::endl;
    
    return -1.0;
#else
    std::cout << "❌ 设备温度: 平台不支持" << std::endl;
    return -1.0;
#endif
}

// 获取网络上传/下载数据
struct NetworkStats {
    unsigned long long bytes_sent;
    unsigned long long bytes_recv;
    unsigned long long bytes_sent_delta;
    unsigned long long bytes_recv_delta;
};

NetworkStats getNetworkStats() {
    static NetworkStats last_stats = {0, 0, 0, 0};
    static bool first_call = true;
    NetworkStats current_stats = {0, 0, 0, 0};

#ifdef __linux__
    // 从 /proc/net/dev 读取网络统计
    std::ifstream file("/proc/net/dev");
    if (!file.is_open()) {
        std::cerr << "无法打开 /proc/net/dev" << std::endl;
        return current_stats;
    }

    std::string line;
    // 跳过前两行标题
    std::getline(file, line);
    std::getline(file, line);

    while (std::getline(file, line)) {
        // 跳过空行
        if (line.empty()) continue;

        // 查找第一个冒号（接口名称分隔符）
        size_t colon_pos = line.find(':');
        if (colon_pos == std::string::npos) continue;

        // 提取接口名称
        std::string interface = line.substr(0, colon_pos);
        // 去除接口名称中的空格
        interface.erase(std::remove(interface.begin(), interface.end(), ' '), interface.end());

        // 跳过回环接口
        if (interface == "lo") continue;

        // 解析统计数据
        std::string data = line.substr(colon_pos + 1);
        unsigned long long recv_bytes, recv_packets, recv_errs, recv_drop, recv_fifo, recv_frame, recv_compressed, recv_multicast;
        unsigned long long sent_bytes, sent_packets, sent_errs, sent_drop, sent_fifo, sent_colls, sent_carrier, sent_compressed;

        if (sscanf(data.c_str(), "%llu %llu %llu %llu %llu %llu %llu %llu %llu %llu %llu %llu %llu %llu %llu %llu",
                   &recv_bytes, &recv_packets, &recv_errs, &recv_drop, &recv_fifo, &recv_frame, &recv_compressed, &recv_multicast,
                   &sent_bytes, &sent_packets, &sent_errs, &sent_drop, &sent_fifo, &sent_colls, &sent_carrier, &sent_compressed) == 16) {
            current_stats.bytes_recv += recv_bytes;
            current_stats.bytes_sent += sent_bytes;
        }
    }

#elif _WIN32
    // Windows平台使用 GetIfTable 获取网络统计（MinGW兼容版本）
    static PMIB_IFTABLE ifTable = NULL;
    static DWORD ifTableSize = 0;

    // 第一次调用获取需要的缓冲区大小
    if (ifTable == NULL) {
        DWORD result = GetIfTable(NULL, &ifTableSize, FALSE);
        if (result == ERROR_INSUFFICIENT_BUFFER) {
            ifTable = (PMIB_IFTABLE)malloc(ifTableSize);
            if (ifTable == NULL) {
                std::cerr << "无法分配网络统计缓冲区" << std::endl;
                return current_stats;
            }
        }
    }

    if (ifTable != NULL) {
        DWORD result = GetIfTable(ifTable, &ifTableSize, FALSE);
        if (result == NO_ERROR) {
            for (DWORD i = 0; i < ifTable->dwNumEntries; i++) {
                // 跳过回环接口
                if (ifTable->table[i].dwType == MIB_IF_TYPE_LOOPBACK) {
                    continue;
                }

                // 累加所有接口的发送和接收字节数
                current_stats.bytes_sent += ifTable->table[i].dwOutOctets;
                current_stats.bytes_recv += ifTable->table[i].dwInOctets;
            }
        }
    }
#endif

    // 计算差值（上传/下载速度）
    if (!first_call) {
        current_stats.bytes_sent_delta = current_stats.bytes_sent - last_stats.bytes_sent;
        current_stats.bytes_recv_delta = current_stats.bytes_recv - last_stats.bytes_recv;
    }

    // 保存当前统计供下次使用
    last_stats = current_stats;
    first_call = false;

    // 显示网络信息
    if (!first_call) {
        std::cout << "✅ 网络流量:" << std::endl;
        std::cout << "   总上传: " << (current_stats.bytes_sent / 1024 / 1024) << " MB" << std::endl;
        std::cout << "   总下载: " << (current_stats.bytes_recv / 1024 / 1024) << " MB" << std::endl;
        std::cout << "   上传速度: " << (current_stats.bytes_sent_delta / 1024) << " KB/s" << std::endl;
        std::cout << "   下载速度: " << (current_stats.bytes_recv_delta / 1024) << " KB/s" << std::endl;
    } else {
        std::cout << "⏳ 网络流量: 正在初始化..." << std::endl;
    }

    return current_stats;
}

// 获取GPU信息
void getGPUInfo() {
#ifdef __linux__
    // 从 /sys/class/drm/card0-*/ 获取GPU信息
    std::string gpu_path = "/sys/class/drm/card0/device";
    
    // 尝试获取GPU名称
    std::string device_path = "/sys/class/drm/card0/device";
    std::ifstream uevent_file(device_path + "/uevent");
    
    std::cout << "✅ GPU信息:" << std::endl;
    
    if (uevent_file.is_open()) {
        std::string line;
        std::getline(uevent_file, line);
        // 解析 uevent 信息，查找 GPU 名称
        std::string gpu_name;
        size_t pos = 0;
        while ((pos = line.find("PCI_SLOT_NAME=", pos)) != std::string::npos) {
            pos += 14;
            size_t end = line.find('\n', pos);
            if (end == std::string::npos) end = line.length();
            gpu_name = line.substr(pos, end - pos);
            break;
        }
        
        if (!gpu_name.empty()) {
            std::cout << "   名称: " << gpu_name << std::endl;
        }
    }
    
    // 获取GPU显存使用
    std::ifstream mem_file("/sys/class/drm/card0/device/mem_info_vram_total");
    if (mem_file.is_open()) {
        unsigned long total_vram = 0;
        mem_file >> total_vram;
        std::cout << "   显存大小: " << (total_vram / 1024 / 1024) << " MB" << std::endl;
    }
    
    std::ifstream mem_used_file("/sys/class/drm/card0/device/mem_info_vram_used");
    if (mem_used_file.is_open()) {
        unsigned long used_vram = 0;
        mem_used_file >> used_vram;
        std::cout << "   显存使用: " << (used_vram / 1024 / 1024) << " MB" << std::endl;
    }
    
    // 列出所有显卡
    std::string cmd = "ls /sys/class/drm/ 2>/dev/null | grep card";
    FILE* pipe = popen(cmd.c_str(), "r");
    if (pipe) {
        char buffer[256];
        std::cout << "   检测到显卡: ";
        bool first = true;
        while (fgets(buffer, sizeof(buffer), pipe)) {
            if (!first) std::cout << ", ";
            buffer[strcspn(buffer, "\n")] = 0;
            std::cout << buffer;
            first = false;
        }
        pclose(pipe);
        std::cout << std::endl;
    }
    
#elif _WIN32
    // Windows平台获取GPU信息
    std::cout << "✅ GPU信息:" << std::endl;
    
    DISPLAY_DEVICE dd;
    dd.cb = sizeof(dd);
    int deviceIndex = 0;
    
    std::cout << "   检测到显卡: ";
    bool first = true;
    std::vector<std::string> gpu_names;
    
    while (EnumDisplayDevices(NULL, deviceIndex, &dd, 0)) {
        if (!first) std::cout << ", ";
        std::cout << dd.DeviceString;
        gpu_names.push_back(dd.DeviceString);
        first = false;
        deviceIndex++;
    }
    std::cout << std::endl;
    
    // 尝试获取显存信息
    bool vram_info_obtained = false;
    
#ifdef USE_NVML
    // 方案1: 使用NVML获取NVIDIA GPU显存
    static bool nvmlInitialized = false;
    static nvmlReturn_t nvml_result = NVML_ERROR_UNINITIALIZED;
    
    if (!nvmlInitialized) {
        nvml_result = nvmlInit();
        nvmlInitialized = true;
    }
    
    if (nvml_result == NVML_SUCCESS) {
                unsigned int device_count = 0;
                nvml_result = nvmlDeviceGetCount(&device_count);
    
                if (nvml_result == NVML_SUCCESS && device_count > 0) {
                    std::cout << "   NVML GPU 信息:" << std::endl;
                    for (unsigned int i = 0; i < device_count; i++) {
                        nvmlDevice_t device;
                        nvml_result = nvmlDeviceGetHandleByIndex(i, &device);
    
                        if (nvml_result == NVML_SUCCESS) {
                            char name[NVML_DEVICE_NAME_BUFFER_SIZE];
                            nvml_result = nvmlDeviceGetName(device, name, sizeof(name));
    
                            if (nvml_result == NVML_SUCCESS) {
                                // 获取显存信息
                                nvmlMemory_t memory;
                                nvml_result = nvmlDeviceGetMemoryInfo(device, &memory);
    
                                if (nvml_result == NVML_SUCCESS) {
                                    unsigned long total_vram = memory.total / 1024 / 1024; // MB
                                    unsigned long used_vram = memory.used / 1024 / 1024;
                                    unsigned long free_vram = memory.free / 1024 / 1024;
                                    double vram_usage = ((double)memory.used / (double)memory.total) * 100.0;
    
                                    std::cout << "   GPU " << i << " (" << name << "):" << std::endl;
                                    std::cout << "     显存: " << used_vram << " MB / " << total_vram << " MB (" << vram_usage << "%)" << std::endl;
    
                                    // 获取 GPU 温度
                                    unsigned int temp = 0;
                                    nvml_result = nvmlDeviceGetTemperature(device, NVML_TEMPERATURE_GPU, &temp);
                                    if (nvml_result == NVML_SUCCESS) {
                                        std::cout << "     温度: " << temp << "°C" << std::endl;
                                    }
    
                                    // 获取 GPU 使用率
                                    nvmlUtilization_t utilization;
                                    nvml_result = nvmlDeviceGetUtilizationRates(device, &utilization);
                                    if (nvml_result == NVML_SUCCESS) {
                                        std::cout << "     GPU 使用率: " << utilization.gpu << "%" << std::endl;
                                        std::cout << "     显存使用率: " << utilization.memory << "%" << std::endl;
                                    }
    
                                    // 获取风扇速度
                                    unsigned int fan_speed = 0;
                                    nvml_result = nvmlDeviceGetFanSpeed(device, &fan_speed);
                                    if (nvml_result == NVML_SUCCESS) {
                                        std::cout << "     风扇速度: " << fan_speed << "%" << std::endl;
                                    }

                                    // 获取功耗
                                    unsigned int power = 0;
                                    nvml_result = nvmlDeviceGetPowerUsage(device, &power);
                                    if (nvml_result == NVML_SUCCESS) {
                                        std::cout << "     功耗: " << (power / 1000.0) << " W" << std::endl;
                                    }

                                    vram_info_obtained = true;
                                }
                            }
                        }
                    }
                }
            }
    
#endif
    
    // 方案3: 使用DXGI获取基本显存信息（通用，但信息有限）
    if (!vram_info_obtained) {
        std::cout << "   显存信息:" << std::endl;
        std::cout << "     状态: 未集成GPU SDK，显存信息不可用" << std::endl;
        std::cout << "     集成方式:" << std::endl;
        
        // 检测是否是NVIDIA GPU
        bool has_nvidia = false;
        bool has_amd = false;
        bool has_intel = false;
        
        for (const auto& name : gpu_names) {
            std::string lower_name = name;
            std::transform(lower_name.begin(), lower_name.end(), lower_name.begin(), ::tolower);
            
            if (lower_name.find("nvidia") != std::string::npos || 
                lower_name.find("geforce") != std::string::npos ||
                lower_name.find("quadro") != std::string::npos ||
                lower_name.find("tesla") != std::string::npos) {
                has_nvidia = true;
            }
            if (lower_name.find("amd") != std::string::npos || 
                lower_name.find("radeon") != std::string::npos) {
                has_amd = true;
            }
            if (lower_name.find("intel") != std::string::npos) {
                has_intel = true;
            }
        }
        
        if (has_nvidia) {
            std::cout << "       NVIDIA GPU: 使用NVML" << std::endl;
            std::cout << "       下载: https://developer.nvidia.com/cuda-toolkit" << std::endl;
            std::cout << "       操作: 取消 USE_NVML 宏定义注释，链接 nvml.lib" << std::endl;
        }
        if (has_amd) {
            std::cout << "       AMD GPU: 使用ADL (AMD Display Library)" << std::endl;
            std::cout << "       下载: AMD GPUOpen网站" << std::endl;
            std::cout << "       操作: 取消 USE_ADL 宏定义注释，链接 atiadlxx.lib" << std::endl;
        }
        if (has_intel) {
            std::cout << "       Intel GPU: 使用Intel GPA或DXGI" << std::endl;
            std::cout << "       下载: https://software.intel.com/gpa" << std::endl;
        }
    }
#else
    std::cout << "❌ GPU信息: 平台不支持" << std::endl;
#endif
}

// 获取磁盘使用率
void getDiskUsage() {
#ifdef _WIN32
    char drives[256];
    DWORD len = GetLogicalDriveStringsA(256, drives);
    
    std::cout << "✅ 磁盘使用率:" << std::endl;
    
    for (char* drive = drives; *drive != '\0'; drive += strlen(drive) + 1) {
        if (GetDriveTypeA(drive) == DRIVE_FIXED) {
            ULARGE_INTEGER freeBytes, totalBytes, availableBytes;
            
            if (GetDiskFreeSpaceExA(drive, &availableBytes, &totalBytes, &freeBytes)) {
                unsigned long long total = totalBytes.QuadPart / 1024 / 1024 / 1024; // GB
                unsigned long long free = freeBytes.QuadPart / 1024 / 1024 / 1024;
                unsigned long long used = total - free;
                double usage = ((double)used / (double)total) * 100.0;
                
                std::cout << "   " << drive << ": ";
                std::cout << used << " GB / " << total << " GB (" << usage << "%)" << std::endl;
            }
        }
    }
#elif __linux__
    std::cout << "✅ 磁盘使用率:" << std::endl;
    std::ifstream file("/proc/mounts");
    if (file.is_open()) {
        std::string line;
        while (std::getline(file, line)) {
            std::istringstream iss(line);
            std::string device, mountPoint, fsType;
            iss >> device >> mountPoint >> fsType;
            
            // 只显示真实的磁盘分区，跳过特殊文件系统
            if (device.find("/dev/") == 0 || device.find("/dev/mapper/") == 0) {
                struct statvfs stat;
                if (statvfs(mountPoint.c_str(), &stat) == 0) {
                    unsigned long long total = stat.f_blocks * stat.f_frsize / 1024 / 1024 / 1024;
                    unsigned long long free = stat.f_bfree * stat.f_frsize / 1024 / 1024 / 1024;
                    unsigned long long used = total - free;
                    double usage = ((double)used / (double)total) * 100.0;
                    
                    std::cout << "   " << mountPoint << " (" << device << "): ";
                    std::cout << used << " GB / " << total << " GB (" << usage << "%)" << std::endl;
                }
            }
        }
    }
#else
    std::cout << "❌ 磁盘使用率: 平台不支持" << std::endl;
#endif
}

// #磁盘温度获取: 需要管理员权限和 SMART 支持，通过 WMI 无法获取
void getDiskTemperature() {
#ifdef _WIN32
    // 建议使用第三方工具：
    // - HWiNFO64: https://www.hwinfo.com/
    // - LibreHardwareMonitor: https://github.com/LibreHardwareMonitor/LibreHardwareMonitor
    // - Open Hardware Monitor: https://openhardwaremonitor.org/
    
    std::cout << "⚠️ 磁盘温度:" << std::endl;
    std::cout << "   建议使用第三方工具获取磁盘温度" << std::endl;
#elif __linux__
    std::cout << "✅ 磁盘温度:" << std::endl;
    // 从 /sys/class/hwmon/hwmon*/temp*_input 读取温度
    for (int i = 0; i < 10; i++) {
        std::string basePath = "/sys/class/hwmon/hwmon" + std::to_string(i);
        std::ifstream nameFile(basePath + "/name");
        if (nameFile.is_open()) {
            std::string name;
            std::getline(nameFile, name);
            nameFile.close();
            
            // 查找磁盘相关的温度传感器
            if (name.find("drive") != std::string::npos || name.find("sata") != std::string::npos) {
                for (int j = 1; j <= 10; j++) {
                    std::string tempPath = basePath + "/temp" + std::to_string(j) + "_input";
                    std::ifstream tempFile(tempPath);
                    if (tempFile.is_open()) {
                        int temp;
                        tempFile >> temp;
                        // 温度单位是毫摄氏度，需要除以1000
                        std::cout << "   " << name << " temp" << j << ": " << (temp / 1000) << "°C" << std::endl;
                        tempFile.close();
                    }
                }
            }
        }
    }
    
    // 尝试使用 smartctl 命令（需要安装 smartmontools）
    std::ifstream checkSctl("which smartctl");
    if (checkSctl.good()) {
        std::system("smartctl --scan | while read line; do echo $line; smartctl -A $line | grep -i temperature; done");
    }
#else
    std::cout << "❌ 磁盘温度: 平台不支持" << std::endl;
#endif
}

// 获取磁盘 I/O 使用率
void getDiskIOUsage() {
#ifdef _WIN32
    static bool initialized = false;
    static PDH_HQUERY diskQuery = NULL;
    static PDH_HCOUNTER diskCounter = NULL;
    
    std::cout << "✅ 磁盘 I/O 使用率:" << std::endl;
    
    if (!initialized) {
        PdhOpenQuery(NULL, 0, &diskQuery);
        
        // 添加总的磁盘 I/O 使用率计数器
        PDH_STATUS status = PdhAddCounterA(diskQuery, "\\PhysicalDisk(_Total)\\% Disk Time", 0, &diskCounter);
        
        if (status == ERROR_SUCCESS) {
            PdhCollectQueryData(diskQuery);
            Sleep(1000); // 等待一秒获取差值
            initialized = true;
        } else {
            std::cout << "   ❌ 无法添加磁盘计数器" << std::endl;
            return;
        }
    }
    
    // 收集数据
    PdhCollectQueryData(diskQuery);
    
    PDH_FMT_COUNTERVALUE counterVal;
    PDH_STATUS status = PdhGetFormattedCounterValue(diskCounter, PDH_FMT_DOUBLE, NULL, &counterVal);
    
    if (status == ERROR_SUCCESS) {
        double usage = counterVal.doubleValue;
        std::cout << "   总计: " << usage << "%" << std::endl;
    } else {
        std::cout << "   ❌ 数据获取失败" << std::endl;
    }
    
    // 获取每个磁盘的 I/O 使用率
    DWORD bufferSize = 0;
    PdhEnumObjectItemsA(NULL, NULL, "PhysicalDisk", NULL, &bufferSize, NULL, NULL, 0, 0);
    
    if (bufferSize > 0) {
        char* buffer = new char[bufferSize];
        PdhEnumObjectItemsA(NULL, NULL, "PhysicalDisk", buffer, &bufferSize, NULL, NULL, PERF_DETAIL_WIZARD, 0);
        
        char* instance = buffer;
        while (*instance != '\0') {
            if (strcmp(instance, "_Total") != 0) {
                char counterPath[256];
                sprintf_s(counterPath, sizeof(counterPath), "\\PhysicalDisk(%s)\\%% Disk Time", instance);
                
                PDH_HCOUNTER tempCounter = NULL;
                status = PdhAddCounterA(diskQuery, counterPath, 0, &tempCounter);
                
                if (status == ERROR_SUCCESS) {
                    PDH_FMT_COUNTERVALUE tempVal;
                    status = PdhGetFormattedCounterValue(tempCounter, PDH_FMT_DOUBLE, NULL, &tempVal);
                    
                    if (status == ERROR_SUCCESS) {
                        std::cout << "   " << instance << ": " << tempVal.doubleValue << "%" << std::endl;
                    }
                    
                    PdhRemoveCounter(tempCounter);
                }
            }
            
            instance += strlen(instance) + 1;
        }
        
        delete[] buffer;
    }
    
#elif __linux__
    std::cout << "✅ 磁盘 I/O 使用率:" << std::endl;
    
    // 从 /proc/diskstats 读取磁盘 I/O 统计
    std::ifstream diskstats("/proc/diskstats");
    if (diskstats.is_open()) {
        std::string line;
        while (std::getline(diskstats, line)) {
            std::istringstream iss(line);
            int major, minor;
            std::string device;
            iss >> major >> minor >> device;
            
            // 只显示实际的磁盘设备，跳过分区
            if (device.find("sd") == 0 || device.find("nvme") == 0 || device.find("vd") == 0) {
                // 读取 I/O 统计数据
                unsigned long reads, reads_merged, reads_sectors, reads_ms;
                unsigned long writes, writes_merged, writes_sectors, writes_ms;
                unsigned long ios, ios_ms, weighted_ios_ms;
                
                iss >> reads >> reads_merged >> reads_sectors >> reads_ms
                    >> writes >> writes_merged >> writes_sectors >> writes_ms
                    >> ios >> ios_ms >> weighted_ios_ms;
                
                // 计算磁盘使用率
                double io_usage = 0.0;
                if (ios_ms > 0) {
                    io_usage = ((double)weighted_ios_ms / (double)ios_ms) * 100.0;
                    if (io_usage > 100.0) io_usage = 100.0;
                }
                
                std::cout << "   " << device << ": " << io_usage << "%" << std::endl;
                std::cout << "     读取: " << reads << " 次, 写入: " << writes << " 次" << std::endl;
            }
        }
    }
    
    // 也可以使用 iostat 命令（如果安装了 sysstat）
    std::ifstream checkIostat("which iostat");
    if (checkIostat.good()) {
        std::cout << "   使用 iostat 命令获取详细信息:" << std::endl;
        std::system("iostat -x 1 1 | grep -E 'Device|sd|nvme|vd'");
    }
#else
    std::cout << "❌ 磁盘 I/O 使用率: 平台不支持" << std::endl;
#endif
}

int main() {
#ifdef _WIN32
    // 设置控制台代码页为 UTF-8
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);
#endif

    std::cout << "========================================" << std::endl;
    std::cout << "跨平台系统监控测试" << std::endl;
    std::cout << "========================================" << std::endl;
    
#ifdef __linux__
    std::cout << "平台: Linux/Android" << std::endl;
    std::cout << "数据源: /proc 文件系统" << std::endl;
#elif _WIN32
    std::cout << "平台: Windows" << std::endl;
    std::cout << "数据源: Windows API + PDH" << std::endl;
#elif __APPLE__
    std::cout << "平台: macOS" << std::endl;
    std::cout << "数据源: sysctl + libproc" << std::endl;
#else
    std::cout << "平台: 未知" << std::endl;
#endif
    
    std::cout << std::endl;
    std::cout << "开始测试参数获取..." << std::endl;
    std::cout << "----------------------------------------" << std::endl;
    
    getCpuUsage();
    std::cout << std::endl;
    
    getSystemMemoryUsage();
    std::cout << std::endl;
    
    getMemoryInfo();
    std::cout << std::endl;
    
    getVirtualMemoryInfo();
    std::cout << std::endl;
    
    getTemperature();
    std::cout << std::endl;
    
    getGPUInfo();
    std::cout << std::endl;
    
    getNetworkStats();
    std::cout << std::endl;
    
    getDiskUsage();
    std::cout << std::endl;
    
    getDiskTemperature();
    std::cout << std::endl;
    
    getDiskIOUsage();
    std::cout << std::endl;
    
    getThreadCount();
    std::cout << std::endl;
    
    getOpenFileCount();
    std::cout << std::endl;
    
    getProcessState();
    std::cout << std::endl;
    
    getProcessUptime();
    std::cout << std::endl;
    
    std::cout << "========================================" << std::endl;
    std::cout << "测试完成！" << std::endl;
    std::cout << "========================================" << std::endl;
    
    return 0;
}