#!/bin/bash

# 一键启动HTTPS服务器脚本
# 支持多平台：Linux, macOS, Android/Termux, Windows (Git Bash/WSL)

# 获取脚本所在目录
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

# 定义颜色
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

echo -e "${BLUE}========================================${NC}"
echo -e "${BLUE}    HTTPS服务器一键启动脚本${NC}"
echo -e "${BLUE}========================================${NC}"
echo ""

# 检查Node.js是否安装
if ! command -v node &> /dev/null; then
    echo -e "${RED}错误: 未检测到Node.js${NC}"
    echo -e "${YELLOW}请先安装Node.js${NC}"
    exit 1
fi

# 检查服务器文件是否存在
SERVER_FILE="$SCRIPT_DIR/server-https.js"
if [ ! -f "$SERVER_FILE" ]; then
    echo -e "${RED}错误: 服务器文件不存在: $SERVER_FILE${NC}"
    exit 1
fi

# 检查SSL证书是否存在
CERT_FILE="$SCRIPT_DIR/cert.pem"
KEY_FILE="$SCRIPT_DIR/key.pem"

if [ ! -f "$CERT_FILE" ] || [ ! -f "$KEY_FILE" ]; then
    echo -e "${YELLOW}警告: SSL证书文件不存在${NC}"
    echo -e "${YELLOW}正在生成自签名证书...${NC}"

    # 生成自签名证书
    openssl req -x509 -newkey rsa:2048 -keyout "$KEY_FILE" -out "$CERT_FILE" -days 365 -nodes -subj "/C=CN/ST=Beijing/L=Beijing/O=WeQTe/OU=Development/CN=localhost" 2>/dev/null

    if [ $? -eq 0 ]; then
        echo -e "${GREEN}SSL证书生成成功${NC}"
    else
        echo -e "${RED}SSL证书生成失败${NC}"
        exit 1
    fi
fi

# 检查是否已有服务器进程在运行（通过端口8443查找）
echo -e "${YELLOW}检查是否有服务器正在运行...${NC}"

# 优先使用pgrep查找node进程，然后检查是否运行server-https.js
PID=""
if command -v pgrep &> /dev/null; then
    PID=$(pgrep -f "node.*server-https.js" 2>/dev/null)
fi

# 如果pgrep未找到，尝试使用ps aux命令（跨平台兼容）
if [ -z "$PID" ]; then
    PID=$(ps aux 2>/dev/null | grep "node.*server-https.js" | grep -v grep | awk '{print $2}')
fi

# 如果ps aux未找到，尝试使用ps -ef命令（部分系统支持）
if [ -z "$PID" ]; then
    PID=$(ps -ef 2>/dev/null | grep "node.*server-https.js" | grep -v grep | awk '{print $2}')
fi

# 如果还是没找到，尝试使用netstat（兼容性）
if [ -z "$PID" ] && command -v netstat &> /dev/null; then
    # 先尝试Windows格式
    PID=$(netstat -ano 2>/dev/null | grep ":8443.*LISTENING" | awk '{print $5}')
    # 如果为空，尝试Linux格式
    if [ -z "$PID" ]; then
        PID=$(netstat -tuln 2>/dev/null | grep ":8443 " | awk '{print $7}' | cut -d'/' -f1)
    fi
fi

# 尝试使用ss命令（netstat的现代替代）
if [ -z "$PID" ] && command -v ss &> /dev/null; then
    PID=$(ss -tlnp 2>/dev/null | grep ":8443" | grep node | awk '{print $7}' | sed 's/.*pid=\([0-9]*\).*/\1/')
fi

# 最后尝试lsof
if [ -z "$PID" ] && command -v lsof &> /dev/null; then
    PID=$(lsof -ti:8443 2>/dev/null)
fi

if [ -n "$PID" ]; then
    echo -e "${YELLOW}发现运行中的服务器进程 (PID: $PID)${NC}"
    echo -e "${YELLOW}正在停止旧服务器...${NC}"

    # Windows: 使用 taskkill
    if command -v taskkill &> /dev/null; then
        taskkill //F //PID $PID > /dev/null 2>&1
    else
        kill $PID 2>/dev/null
        sleep 1

        # 如果进程仍在运行，强制终止
        if ps -p $PID > /dev/null 2>&1; then
            kill -9 $PID 2>/dev/null
            sleep 1
        fi
    fi
    echo -e "${GREEN}旧服务器已停止${NC}"
fi

# 获取本机IPv4地址
get_ipv4_address() {
    local ipv4_address='localhost'
    
    # 检查操作系统类型
    if [[ "$OSTYPE" == "darwin"* ]]; then
        # macOS
        ipv4_address=$(ipconfig getifaddr en0 2>/dev/null || echo 'localhost')
    elif [[ "$OSTYPE" == "msys" ]] || [[ "$OSTYPE" == "win32" ]] || [[ "$OSTYPE" == "cygwin" ]]; then
        # Windows (Git Bash, MSYS, Cygwin)
        # 使用 ipconfig 获取 IPv4 地址
        # 优先级：WLAN (只匹配有 IPv4 Address 的) > 无线网络 > 以太网
        
        # 查找 WLAN 适配器中实际有 IPv4 地址的（排除断开连接的）
        ipv4_address=$(ipconfig | grep -A 10 "Wireless LAN adapter WLAN" | grep "IPv4 Address" | awk '{print $NF}' | head -n1)
        
        # 如果没找到，尝试查找"无线网络适配器"
        if [ -z "$ipv4_address" ]; then
            ipv4_address=$(ipconfig | grep -A 10 "无线网络适配器" | grep "IPv4 Address" | awk '{print $NF}' | head -n1)
        fi
        
        # 如果没找到，尝试查找以太网（排除断开连接的）
        if [ -z "$ipv4_address" ]; then
            ipv4_address=$(ipconfig | grep -A 10 "Ethernet adapter" | grep "IPv4 Address" | awk '{print $NF}' | head -n1)
        fi
        
        # 如果没找到，查找所有网络适配器，但排除虚拟网络适配器
        # 排除 VMware、Hyper-V、VirtualBox 等虚拟网卡
        if [ -z "$ipv4_address" ]; then
            ipv4_address=$(ipconfig | grep -B 20 "IPv4 Address" | \
                           grep -E "^(无线局域网适配器|Wireless LAN adapter|以太网适配器|Ethernet adapter)" | \
                           grep -vEi "(VMware|VirtualBox|Hyper-V|vEthernet|Virtual)" | \
                           head -n1 | \
                           while read line; do
                               adapter=$(echo "$line" | sed 's/适配器.*$//;s/adapter.*$//' | tr -d ' ')
                               ipconfig | grep -A 20 "$adapter" | grep "IPv4 Address" | awk '{print $NF}' | head -n1
                           done)
        fi
    elif [[ "$OSTYPE" == "linux-gnu"* ]]; then
        # Linux
        if command -v ip &> /dev/null; then
            local interfaces=$(ip -4 addr show 2>/dev/null | grep -oP '(?<=inet\s)\d+(\.\d+){3}')
            if [ -n "$interfaces" ]; then
                # 过滤掉 127.0.0.1
                ipv4_address=$(echo "$interfaces" | grep -v "^127\." | head -n1)
            fi
        elif command -v ifconfig &> /dev/null; then
            # 使用 ifconfig 作为备选
            ipv4_address=$(ifconfig 2>/dev/null | grep "inet " | grep -v "127.0.0.1" | awk '{print $2}' | head -n1)
        fi
    fi
    
    # 如果还是localhost，尝试使用hostname -I
    if [ "$ipv4_address" == 'localhost' ] || [ -z "$ipv4_address" ]; then
        ipv4_address=$(hostname -I 2>/dev/null | awk '{print $1}')
    fi
    
    # 如果仍然为空，返回 localhost
    if [ -z "$ipv4_address" ]; then
        ipv4_address='localhost'
    fi
    
    echo "$ipv4_address"
}

# 启动服务器
echo ""
echo -e "${GREEN}正在启动HTTPS服务器...${NC}"
echo ""

cd "$SCRIPT_DIR"

# 在后台启动服务器并记录输出
nohup node server-https.js > https-server.log 2>&1 &
SERVER_PID=$!

# 等待服务器启动
sleep 2

# 获取IPv4地址
LOCAL_IP=$(get_ipv4_address)

# 检查服务器是否成功启动
if ps -p $SERVER_PID > /dev/null 2>&1; then
    echo -e "${GREEN}========================================${NC}"
    echo -e "${GREEN}HTTPS服务器启动成功！${NC}"
    echo -e "${GREEN}========================================${NC}"
    echo ""
    echo -e "${BLUE}服务器信息：${NC}"
    echo -e "  进程ID: ${GREEN}$SERVER_PID${NC}"
    echo -e "  日志文件: ${GREEN}$SCRIPT_DIR/https-server.log${NC}"
    echo ""
    echo -e "${BLUE}访问地址：${NC}"
    echo -e "  本地访问: ${GREEN}https://localhost:8443${NC}"
    echo -e "  网关访问: ${GREEN}https://0.0.0.0:8443${NC}"
    echo -e "  局域网访问: ${GREEN}https://${LOCAL_IP}:8443${NC}"
    echo ""
    echo -e "${BLUE}服务器输出：${NC}"
    echo -e "${GREEN}----------------------------------------${NC}"

    # 输出服务器日志内容
    cat "$SCRIPT_DIR/https-server.log"

    echo -e "${GREEN}----------------------------------------${NC}"
    echo ""
    echo -e "${YELLOW}提示：${NC}"
    echo -e "  • 浏览器会提示证书不安全，请点击'高级'然后'继续访问'"
    echo -e "  • 查看日志: ${GREEN}tail -f $SCRIPT_DIR/https-server.log${NC}"
    echo -e "  • 停止服务器: ${GREEN}kill $SERVER_PID${NC}"
    echo ""
    echo -e "${BLUE}========================================${NC}"

    printf "服务器已后台启动，按任意键继续..."
    read -n1 -s -r
    echo ""
    echo -e "${GREEN}继续执行脚本！${NC}"
else
    echo -e "${RED}========================================${NC}"
    echo -e "${RED}HTTPS服务器启动失败！${NC}"
    echo -e "${RED}========================================${NC}"
    echo ""
    echo -e "${YELLOW}查看日志: $SCRIPT_DIR/https-server.log${NC}"
    echo ""
    exit 1
fi