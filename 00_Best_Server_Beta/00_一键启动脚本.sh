#!/bin/bash

# 00_一键启动脚本.sh
# 功能：启动web服务器和https代理，自动清理端口

# 颜色定义
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

# 配置
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
WEB_SERVER_PORT=8080
HTTPS_PROXY_PORT=8443
WEB_SERVER_DIR="$SCRIPT_DIR/web/build"
HTTPS_PROXY_CERT="$SCRIPT_DIR/web/cert.pem"
HTTPS_PROXY_KEY="$SCRIPT_DIR/web/key.pem"
LOG_DIR="$SCRIPT_DIR/web/uploads/logs"
WEB_SERVER_LOG="$LOG_DIR/web_server.log"
HTTPS_PROXY_LOG="$LOG_DIR/https_proxy.log"

# 函数：通过端口查找所有进程PID
find_pids_by_port() {
    local port=$1
    local pids=""
    
    # 方法1: 使用 lsof
    local pid=$(lsof -ti :$port 2>/dev/null)
    if [ -n "$pid" ]; then
        pids="$pids $pid"
    fi
    
    # 方法2: 使用 netstat
    if command -v netstat &> /dev/null; then
        pid=$(netstat -tlnp 2>/dev/null | grep ":$port " | awk '{print $7}' | cut -d'/' -f1)
        if [ -n "$pid" ] && [ "$pid" != "-" ]; then
            pids="$pids $pid"
        fi
    fi
    
    # 方法3: 使用 ss
    if command -v ss &> /dev/null; then
        pid=$(ss -tlnp 2>/dev/null | grep ":$port " | awk '{print $7}' | cut -d'/' -f1)
        if [ -n "$pid" ] && [ "$pid" != "-" ]; then
            pids="$pids $pid"
        fi
    fi
    
    # 方法4: 使用 pgrep 根据进程名，并过滤掉脚本本身
    if [ "$port" = "$WEB_SERVER_PORT" ]; then
        # 只匹配 ./web_server 或 web_server，不匹配脚本
        pid=$(pgrep -f "./web_server$" 2>/dev/null)
        if [ -n "$pid" ]; then
            pids="$pids $pid"
        fi
    elif [ "$port" = "$HTTPS_PROXY_PORT" ]; then
        # 只匹配 ./https_proxy，不匹配脚本
        pid=$(pgrep -f "./https_proxy" 2>/dev/null | head -1)
        if [ -n "$pid" ]; then
            pids="$pids $pid"
        fi
    fi
    
    # 去重并返回
    echo "$pids" | tr ' ' '\n' | grep -v '^$' | sort -u | tr '\n' ' '
}

# 函数：强制终止进程
kill_process_forcefully() {
    local pid=$1
    local port=$2
    
    # 先尝试正常终止
    if kill -0 $pid 2>/dev/null; then
        kill -TERM $pid 2>/dev/null
        sleep 0.5
        
        # 如果还在运行，强制终止
        if kill -0 $pid 2>/dev/null; then
            kill -9 $pid 2>/dev/null
            sleep 0.2
        fi
        
        # 最后检查
        if kill -0 $pid 2>/dev/null; then
            echo -e "${RED}警告: 无法终止进程 $pid (端口 $port)${NC}"
            return 1
        else
            echo -e "${GREEN}成功终止进程 $pid (端口 $port)${NC}"
            return 0
        fi
    fi
}

# 函数：清理端口
cleanup_port() {
    local port=$1
    local pids=$(find_pids_by_port $port)
    
    if [ -n "$pids" ]; then
        echo -e "${YELLOW}清理端口 $port，发现进程: $pids${NC}"
        for pid in $pids; do
            if [ -n "$pid" ]; then
                kill_process_forcefully $pid $port
            fi
        done
        
        # 额外等待，确保端口释放
        sleep 1
        
        # 再次检查端口是否还被占用
        local remaining_pids=$(find_pids_by_port $port)
        if [ -n "$remaining_pids" ]; then
            echo -e "${RED}警告: 端口 $port 仍被占用: $remaining_pids${NC}"
        else
            echo -e "${GREEN}端口 $port 已释放${NC}"
        fi
    else
        echo -e "${GREEN}端口 $port 未被占用${NC}"
    fi
}

# 函数：清理所有相关进程
cleanup_all() {
    echo -e "${YELLOW}清理旧的进程和端口...${NC}"
    
    # 直接按端口清理，不使用 pkill -f（会终止脚本本身）
    cleanup_port $WEB_SERVER_PORT
    cleanup_port $HTTPS_PROXY_PORT
    
    # 额外等待，确保资源释放
    sleep 1
}

# 函数：启动web服务器
start_web_server() {
    cd "$WEB_SERVER_DIR"
    if [ ! -f "web_server" ]; then
        echo -e "${RED}错误: 找不到web_server可执行文件${NC}"
        return 1
    fi
    
    # 先确保端口没有被占用
    local existing_pids=$(find_pids_by_port $WEB_SERVER_PORT)
    if [ -n "$existing_pids" ]; then
        echo -e "${YELLOW}端口 $WEB_SERVER_PORT 仍被占用，强制清理: $existing_pids${NC}"
        for pid in $existing_pids; do
            kill_process_forcefully $pid $WEB_SERVER_PORT
        done
        sleep 1
    fi
    
    # 确保日志目录存在并清空日志文件
    mkdir -p "$LOG_DIR"
    > "$WEB_SERVER_LOG"
    
    # 直接后台启动，将输出重定向到日志文件
    nohup ./web_server > "$WEB_SERVER_LOG" 2>&1 &
    local server_pid=$!
    
    echo -e "${YELLOW}Web服务器正在启动 (PID: $server_pid)...${NC}"
    
    # 等待启动，最多等待5秒
    local max_wait=5
    local wait_count=0
    local started=0
    
    while [ $wait_count -lt $max_wait ]; do
        sleep 1
        wait_count=$((wait_count + 1))
        
        # 检查进程是否还在运行
        if ! kill -0 $server_pid 2>/dev/null; then
            echo -e "${RED}Web服务器进程已退出${NC}"
            return 1
        fi
        
        # 检查端口是否已监听
        local check_pids=$(find_pids_by_port $WEB_SERVER_PORT)
        if [ -n "$check_pids" ]; then
            # 检查是否包含我们的进程
            for pid in $check_pids; do
                if [ "$pid" = "$server_pid" ]; then
                    started=1
                    break
                fi
            done
        fi
        
        if [ $started -eq 1 ]; then
            break
        fi
    done
    
    if [ $started -eq 1 ]; then
        # 从日志文件中提取启动信息
        if [ -f "$WEB_SERVER_LOG" ]; then
            # 提取启动成功的关键信息
            local startup_info=$(grep -A 20 "Server started successfully" "$WEB_SERVER_LOG" | head -n 20)
            if [ -n "$startup_info" ]; then
                echo "$startup_info"
            fi
        fi
        return 0
    else
        echo -e "${RED}Web服务器启动超时${NC}"
        # 检查进程状态
        if kill -0 $server_pid 2>/dev/null; then
            echo -e "${YELLOW}进程仍在运行，但端口未监听，尝试终止...${NC}"
            kill -9 $server_pid 2>/dev/null
        fi
        return 1
    fi
}

# 函数：启动https代理
start_https_proxy() {
    cd "$WEB_SERVER_DIR"
    
    # 检查证书文件
    if [ ! -f "$HTTPS_PROXY_CERT" ]; then
        echo -e "${RED}错误: 找不到证书文件 $HTTPS_PROXY_CERT${NC}"
        return 1
    fi
    
    if [ ! -f "$HTTPS_PROXY_KEY" ]; then
        echo -e "${RED}错误: 找不到私钥文件 $HTTPS_PROXY_KEY${NC}"
        return 1
    fi
    
    if [ ! -f "./https_proxy" ]; then
        echo -e "${RED}错误: 找不到https_proxy可执行文件${NC}"
        return 1
    fi
    
    # 先确保端口没有被占用
    local existing_pids=$(find_pids_by_port $HTTPS_PROXY_PORT)
    if [ -n "$existing_pids" ]; then
        echo -e "${YELLOW}端口 $HTTPS_PROXY_PORT 仍被占用，强制清理: $existing_pids${NC}"
        for pid in $existing_pids; do
            kill_process_forcefully $pid $HTTPS_PROXY_PORT
        done
        sleep 1
    fi
    
    # 确保日志目录存在并清空日志文件
    mkdir -p "$LOG_DIR"
    > "$HTTPS_PROXY_LOG"
    
    # 后台启动，将输出重定向到日志文件
    nohup ./https_proxy $HTTPS_PROXY_PORT "$HTTPS_PROXY_CERT" "$HTTPS_PROXY_KEY" $WEB_SERVER_PORT 127.0.0.1 > "$HTTPS_PROXY_LOG" 2>&1 &
    local proxy_pid=$!
    
    echo -e "${YELLOW}HTTPS代理正在启动 (PID: $proxy_pid)...${NC}"
    
    # 等待启动，最多等待5秒
    local max_wait=5
    local wait_count=0
    local started=0
    
    while [ $wait_count -lt $max_wait ]; do
        sleep 1
        wait_count=$((wait_count + 1))
        
        # 检查进程是否还在运行
        if ! kill -0 $proxy_pid 2>/dev/null; then
            echo -e "${RED}HTTPS代理进程已退出${NC}"
            return 1
        fi
        
        # 检查端口是否已监听
        local check_pids=$(find_pids_by_port $HTTPS_PROXY_PORT)
        if [ -n "$check_pids" ]; then
            # 检查是否包含我们的进程
            for pid in $check_pids; do
                if [ "$pid" = "$proxy_pid" ]; then
                    started=1
                    break
                fi
            done
        fi
        
        if [ $started -eq 1 ]; then
            break
        fi
    done
    
    if [ $started -eq 1 ]; then
        return 0
    else
        echo -e "${RED}HTTPS代理启动超时${NC}"
        # 检查进程状态
        if kill -0 $proxy_pid 2>/dev/null; then
            echo -e "${YELLOW}进程仍在运行，但端口未监听，尝试终止...${NC}"
            kill -9 $proxy_pid 2>/dev/null
        fi
        return 1
    fi
}

# 函数：停止所有服务
stop_all() {
    echo -e "${YELLOW}停止所有服务...${NC}"
    
    # 先按端口清理
    cleanup_port $WEB_SERVER_PORT
    cleanup_port $HTTPS_PROXY_PORT
    
    # 再按进程名清理
    local web_pids=$(pgrep -f "web_server" 2>/dev/null)
    if [ -n "$web_pids" ]; then
        echo -e "${YELLOW}终止 web_server 进程: $web_pids${NC}"
        for pid in $web_pids; do
            kill_process_forcefully $pid "web_server"
        done
    fi
    
    local https_pids=$(pgrep -f "https_proxy" 2>/dev/null)
    if [ -n "$https_pids" ]; then
        echo -e "${YELLOW}终止 https_proxy 进程: $https_pids${NC}"
        for pid in $https_pids; do
            kill_process_forcefully $pid "https_proxy"
        done
    fi
    
    # 最后使用 pkill 强制清理
    pkill -9 -f "web_server" 2>/dev/null
    pkill -9 -f "https_proxy" 2>/dev/null
    
    echo -e "${GREEN}所有服务已停止${NC}"
}

# 主函数
main() {
    case "${1:-start}" in
        start)
            echo "========================================="
            echo "   一键启动Web服务器和HTTPS代理"
            echo "========================================="
            
            cleanup_all
            
            echo -e "${GREEN}启动Web服务器 (端口 $WEB_SERVER_PORT)...${NC}"
            start_web_server
            local web_result=$?
            
            if [ $web_result -eq 0 ]; then
                local web_pid=$(find_pids_by_port $WEB_SERVER_PORT | head -1)
                echo -e "${GREEN}Web服务器启动成功 (PID: $web_pid)${NC}"
                
                echo -e "${GREEN}启动HTTPS代理 (端口 $HTTPS_PROXY_PORT)...${NC}"
                start_https_proxy
                if [ $? -eq 0 ]; then
                    local https_pid=$(find_pids_by_port $HTTPS_PROXY_PORT | head -1)
                    echo -e "${GREEN}HTTPS代理启动成功 (PID: $https_pid)${NC}"
                    
                    echo ""
                    echo "========================================="
                    echo -e "${GREEN}   所有服务启动成功！${NC}"
                    echo "========================================="
                    
                    # 显示HTTP地址
                    echo "HTTP服务器 (端口 $WEB_SERVER_PORT):"
                    echo "  http://127.0.0.1:$WEB_SERVER_PORT"
                    
                    # 从日志文件中提取IPv4地址
                    if [ -f "$WEB_SERVER_LOG" ]; then
                        grep -oE 'http://[0-9]{1,3}\.[0-9]{1,3}\.[0-9]{1,3}\.[0-9]{1,3}:[0-9]+' "$WEB_SERVER_LOG" | sed 's|http://||' | sed 's|:[0-9]*$||' | grep -v '127\.0\.0\.1' | grep -v '0\.0\.0\.0' | sort -u | while read ip; do
                            echo "  http://$ip:$WEB_SERVER_PORT"
                        done
                    fi
                    
                    # 从日志文件中提取IPv6地址
                    if [ -f "$WEB_SERVER_LOG" ]; then
                        grep -oE 'http://\[[0-9a-fA-F:]+\]:[0-9]+' "$WEB_SERVER_LOG" | sed 's|http://\[||' | sed 's|\]:[0-9]*$||' | grep -v '::1' | grep -v '^::$' | sort -u | while read ipv6; do
                            echo "  http://[$ipv6]:$WEB_SERVER_PORT"
                        done
                    fi
                    
                    # 显示HTTPS地址
                    echo "HTTPS代理 (端口 $HTTPS_PROXY_PORT):"
                    echo "  https://127.0.0.1:$HTTPS_PROXY_PORT"
                    
                    # 从日志文件中提取IPv4地址
                    if [ -f "$WEB_SERVER_LOG" ]; then
                        grep -oE 'http://[0-9]{1,3}\.[0-9]{1,3}\.[0-9]{1,3}\.[0-9]{1,3}:[0-9]+' "$WEB_SERVER_LOG" | sed 's|http://||' | sed 's|:[0-9]*$||' | grep -v '127\.0\.0\.1' | grep -v '0\.0\.0\.0' | sort -u | while read ip; do
                            echo "  https://$ip:$HTTPS_PROXY_PORT"
                        done
                    fi
                    
                    # 从日志文件中提取IPv6地址
                    if [ -f "$WEB_SERVER_LOG" ]; then
                        grep -oE 'http://\[[0-9a-fA-F:]+\]:[0-9]+' "$WEB_SERVER_LOG" | sed 's|http://\[||' | sed 's|\]:[0-9]*$||' | grep -v '::1' | grep -v '^::$' | sort -u | while read ipv6; do
                            echo "  https://[$ipv6]:$HTTPS_PROXY_PORT"
                        done
                    fi
                    
                    echo "========================================="
                    exit 0
                else
                    echo -e "${RED}HTTPS代理启动失败${NC}"
                fi
            else
                echo -e "${RED}Web服务器启动失败${NC}"
            fi
            ;;
            
        stop)
            stop_all
            ;;
            
        restart)
            stop_all
            $0 start
            ;;
            
        status)
            echo "========================================="
            echo "   服务状态"
            echo "========================================="
            
            local web_pids=$(find_pids_by_port $WEB_SERVER_PORT)
            local https_pids=$(find_pids_by_port $HTTPS_PROXY_PORT)
            
            echo "Web服务器 (端口 $WEB_SERVER_PORT):"
            if [ -n "$web_pids" ]; then
                local running_count=0
                for pid in $web_pids; do
                    if ps -p $pid > /dev/null 2>&1; then
                        echo -e "  ${GREEN}运行中${NC} (PID: $pid)"
                        running_count=$((running_count + 1))
                    fi
                done
                if [ $running_count -eq 0 ]; then
                    echo -e "  ${RED}未运行${NC}"
                fi
            else
                echo -e "  ${RED}未运行${NC}"
            fi
            
            echo "HTTPS代理 (端口 $HTTPS_PROXY_PORT):"
            if [ -n "$https_pids" ]; then
                local running_count=0
                for pid in $https_pids; do
                    if ps -p $pid > /dev/null 2>&1; then
                        echo -e "  ${GREEN}运行中${NC} (PID: $pid)"
                        running_count=$((running_count + 1))
                    fi
                done
                if [ $running_count -eq 0 ]; then
                    echo -e "  ${RED}未运行${NC}"
                fi
            else
                echo -e "  ${RED}未运行${NC}"
            fi
            
            echo "========================================="
            ;;
            
        *)
            echo "用法: $0 {start|stop|restart|status}"
            echo ""
            echo "命令说明:"
            echo "  start   - 启动所有服务（默认）"
            echo "  stop    - 停止所有服务"
            echo "  restart - 重启所有服务"
            echo "  status  - 查看服务状态"
            exit 1
            ;;
    esac
}

main "$@"