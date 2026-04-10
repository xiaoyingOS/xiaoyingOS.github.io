#include <cstdio>
#include <string>
#include <vector>
#include <sstream>
#include <iostream>

double get_real_cpu_usage() {
    // 使用top命令获取进程的CPU使用率
    FILE* pipe = popen("top -bn1 2>/dev/null | awk 'NR>7 && $9 != \"0.0\" {print $9}'", "r");
    if (!pipe) {
        return 0.0;
    }
    
    char buffer[4096];
    double total_cpu = 0.0;
    
    while (fgets(buffer, sizeof(buffer), pipe)) {
        double cpu_percent = 0.0;
        if (sscanf(buffer, "%lf", &cpu_percent) == 1) {
            total_cpu += cpu_percent;
        }
    }
    pclose(pipe);
    
    return total_cpu;
}

int main() {
    double cpu = get_real_cpu_usage();
    printf("CPU Usage: %.1f%%\n", cpu);
    return 0;
}
