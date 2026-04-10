#!/bin/bash
cd /data/data/com.termux/files/home/Server/web

# 清理所有进程
ps aux | grep -E "simple_upload|web_server|test_server|:8080" | grep -v grep | awk '{print $2}' | xargs -r kill -9 2>/dev/null

# 创建最简单的测试服务器
cat > test_server.c << 'TESTEOF'
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>

#define PORT 8080
#define BUFFER_SIZE 65536

int main() {
    int server_fd, client_fd;
    struct sockaddr_in server_addr, client_addr;
    socklen_t addr_len = sizeof(client_addr);
    char buffer[BUFFER_SIZE];
    ssize_t bytes_read;
    
    // 创建socket
    server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) {
        perror("socket");
        return 1;
    }
    
    // 设置地址重用
    int opt = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    
    // 绑定地址
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(PORT);
    
    if (bind(server_fd, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        perror("bind");
        close(server_fd);
        return 1;
    }
    
    // 监听
    if (listen(server_fd, 10) < 0) {
        perror("listen");
        close(server_fd);
        return 1;
    }
    
    printf("Server running on port %d\n", PORT);
    fflush(stdout);
    
    while (1) {
        client_fd = accept(server_fd, (struct sockaddr*)&client_addr, &addr_len);
        if (client_fd < 0) {
            continue;
        }
        
        bytes_read = recv(client_fd, buffer, BUFFER_SIZE - 1, 0);
        if (bytes_read > 0) {
            buffer[bytes_read] = '\0';
            
            // 简单HTTP响应
            const char *response = 
                "HTTP/1.1 200 OK\r\n"
                "Content-Type: text/html\r\n"
                "Connection: close\r\n"
                "\r\n"
                "<html><body><h1>Upload Test Server</h1><p>Server is working!</p></body></html>";
            
            send(client_fd, response, strlen(response), 0);
        }
        
        close(client_fd);
    }
    
    close(server_fd);
    return 0;
}
TESTEOF

# 编译
gcc test_server.c -o test_server 2>&1

# 启动服务器
./test_server > test_server.log 2>&1 &
SERVER_PID=$!

echo "Server PID: $SERVER_PID"
sleep 2

# 测试
echo "Testing server..."
curl -v http://127.0.0.1:8080/ 2>&1 | head -20

# 清理
kill $SERVER_PID 2>/dev/null
