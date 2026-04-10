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
