#!/bin/bash

# ========================================
# 多平台一键启动HTTPS服务器脚本
# 支持: Linux, macOS, Android Termux
# ========================================

# 颜色定义
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# 打印带颜色的信息
print_info() {
    echo -e "${BLUE}[INFO]${NC} $1"
}

print_success() {
    echo -e "${GREEN}[SUCCESS]${NC} $1"
}

print_warning() {
    echo -e "${YELLOW}[WARNING]${NC} $1"
}

print_error() {
    echo -e "${RED}[ERROR]${NC} $1"
}

# 检测操作系统类型
detect_os() {
    # 方法1: 检查PREFIX变量（Termux专用）
    if [ "$PREFIX" = "/data/data/com.termux/files/usr" ]; then
        echo "termux"
        return
    fi
    
    # 方法2: 使用uname -o检测Android系统
    if [ "$(uname -o 2>/dev/null)" = "Android" ]; then
        echo "termux"
        return
    fi
    
    # 方法3: 检查macOS
    if [ "$(uname)" = "Darwin" ]; then
        echo "macos"
        return
    fi
    
    # 方法4: 检查/proc/version（传统Linux检测）
    if [ -f /proc/version ]; then
        if grep -q "Android" /proc/version 2>/dev/null; then
            echo "termux"
        else
            echo "linux"
        fi
    else
        echo "unknown"
    fi
}

# 自动安装Node.js
check_and_install_nodejs() {
    print_info "检查Node.js是否安装..."
    
    if command -v node &> /dev/null; then
        NODE_VERSION=$(node -v)
        print_success "Node.js已安装: $NODE_VERSION"
        return 0
    else
        print_warning "Node.js未安装，正在自动安装..."
        
        OS=$(detect_os)
        
        case $OS in
            "termux")
                print_info "在Termux环境中安装Node.js..."
                pkg install -y nodejs
                ;;
            "linux")
                print_info "在Linux环境中安装Node.js..."
                if command -v apt-get &> /dev/null; then
                    sudo apt-get update && sudo apt-get install -y nodejs npm
                elif command -v yum &> /dev/null; then
                    sudo yum install -y nodejs npm
                else
                    print_error "无法自动安装Node.js，请手动安装"
                    return 1
                fi
                ;;
            "macos")
                print_info "在macOS环境中安装Node.js..."
                if command -v brew &> /dev/null; then
                    brew install node
                else
                    print_error "请先安装Homebrew: /bin/bash -c \"\$(curl -fsSL https://raw.githubusercontent.com/Homebrew/install/HEAD/install.sh)\""
                    return 1
                fi
                ;;
            *)
                print_error "无法识别的操作系统，请手动安装Node.js"
                return 1
                ;;
        esac
        
        # 验证安装
        if command -v node &> /dev/null; then
            NODE_VERSION=$(node -v)
            print_success "Node.js安装成功: $NODE_VERSION"
            return 0
        else
            print_error "Node.js安装失败"
            return 1
        fi
    fi
}

# 自动安装ffmpeg
check_and_install_ffmpeg() {
    print_info "检查ffmpeg是否安装..."
    
    if command -v ffmpeg &> /dev/null; then
        FFmpeg_VERSION=$(ffmpeg -version 2>&1 | head -n 1)
        print_success "ffmpeg已安装: $FFmpeg_VERSION"
        return 0
    else
        print_warning "ffmpeg未安装，正在自动安装..."
        
        OS=$(detect_os)
        
        case $OS in
            "termux")
                print_info "在Termux环境中安装ffmpeg..."
                pkg install -y ffmpeg
                ;;
            "linux")
                print_info "在Linux环境中安装ffmpeg..."
                if command -v apt-get &> /dev/null; then
                    sudo apt-get update && sudo apt-get install -y ffmpeg
                elif command -v yum &> /dev/null; then
                    sudo yum install -y ffmpeg
                else
                    print_error "无法自动安装ffmpeg，请手动安装"
                    return 1
                fi
                ;;
            "macos")
                print_info "在macOS环境中安装ffmpeg..."
                if command -v brew &> /dev/null; then
                    brew install ffmpeg
                else
                    print_error "请先安装Homebrew"
                    return 1
                fi
                ;;
            *)
                print_error "无法识别的操作系统，请手动安装ffmpeg"
                return 1
                ;;
        esac
        
        # 验证安装
        if command -v ffmpeg &> /dev/null; then
            FFmpeg_VERSION=$(ffmpeg -version 2>&1 | head -n 1)
            print_success "ffmpeg安装成功: $FFmpeg_VERSION"
            return 0
        else
            print_error "ffmpeg安装失败"
            return 1
        fi
    fi
}

# 检查并安装依赖
install_dependencies() {
    print_info "检查项目依赖..."
    
    if [ ! -d "node_modules" ] || [ ! -f "node_modules/.package-lock.json" ]; then
        print_warning "依赖未安装，正在安装..."
        
        if npm install; then
            print_success "依赖安装完成！"
        else
            print_error "依赖安装失败！"
            exit 1
        fi
    else
        print_success "依赖已安装，跳过安装步骤。"
    fi
}

# 检查SSL证书
check_ssl_cert() {
    print_info "检查SSL证书..."
    
    if [ -f "cert.pem" ] && [ -f "key.pem" ]; then
        print_success "SSL证书文件存在"
        return 0
    else
        print_error "SSL证书文件不存在！"
        print_info "请确保 cert.pem 和 key.pem 文件在当前目录"
        return 1
    fi
}

# 检查端口是否被占用
check_port() {
    PORT=8691
    print_info "检查端口 $PORT 是否被占用..."
    
    # 查找占用端口的进程
    FOUND_PROCESS=false
    
    # 方法1: 使用lsof (Linux/macOS)
    if command -v lsof &> /dev/null; then
        PIDS=$(lsof -ti :$PORT 2>/dev/null)
        if [ -n "$PIDS" ]; then
            FOUND_PROCESS=true
            print_warning "端口 $PORT 已被占用"
            print_info "正在尝试停止占用端口的进程..."
            
            for PID in $PIDS; do
                print_info "找到进程 $PID"
                kill -9 $PID 2>/dev/null
                if [ $? -eq 0 ]; then
                    print_success "已停止进程 $PID"
                fi
            done
        fi
    fi
    
    # 方法2: 使用netstat (Linux/Termux/Windows)
    if command -v netstat &> /dev/null; then
        if netstat -tuln 2>/dev/null | grep -q ":$PORT "; then
            if [ "$FOUND_PROCESS" = false ]; then
                FOUND_PROCESS=true
                print_warning "端口 $PORT 已被占用"
                print_info "正在尝试停止占用端口的进程..."
            fi
            
            # 查找相关进程
            PIDS=$(ps aux | grep "node https-server.js" | grep -v grep | awk '{print $2}')
            if [ -n "$PIDS" ]; then
                for PID in $PIDS; do
                    print_info "找到进程 $PID"
                    kill -9 $PID 2>/dev/null
                    if [ $? -eq 0 ]; then
                        print_success "已停止进程 $PID"
                    fi
                done
            fi
        fi
    fi
    
    # 方法3: 直接查找node进程
    if [ "$FOUND_PROCESS" = false ]; then
        PIDS=$(ps aux | grep "node https-server.js" | grep -v grep | awk '{print $2}')
        if [ -n "$PIDS" ]; then
            FOUND_PROCESS=true
            print_warning "发现运行中的node服务器进程"
            print_info "正在尝试停止..."
            
            for PID in $PIDS; do
                print_info "找到进程 $PID"
                kill -9 $PID 2>/dev/null
                if [ $? -eq 0 ]; then
                    print_success "已停止进程 $PID"
                fi
            done
        fi
    fi
    
    # 等待端口释放
    if [ "$FOUND_PROCESS" = true ]; then
        print_info "等待端口释放..."
        sleep 2
    fi
    
    print_success "端口 $PORT 准备就绪"
}

# 启动服务器
start_server() {
    print_info "正在启动HTTPS服务器..."
    echo "========================================"
    echo ""
    
    # 检查PID文件是否存在
    if [ -f "server.pid" ]; then
        OLD_PID=$(cat server.pid)
        if ps -p $OLD_PID > /dev/null 2>&1; then
            print_warning "服务器已在运行中（PID: $OLD_PID）"
            print_info "如需重启，请先执行: kill $OLD_PID"
            return 0
        else
            print_info "发现旧的PID文件，清理中..."
            rm -f server.pid
        fi
    fi
    
    # 在后台启动服务器，同时使用 tee 显示输出到终端
    nohup node https-server.js 2>&1 | tee -a server.log &
    
    # 保存PID
    echo $! > server.pid
    PID=$(cat server.pid)
    
    # 等待一下确保启动成功
    sleep 2
    
    # 检查进程是否还在运行
    if ps -p $PID > /dev/null 2>&1; then
        print_success "服务器已在后台启动！"
        print_info "进程ID: $PID"
        print_info "日志文件: server.log"
        print_info "停止服务器: kill $PID"
        echo ""
        print_info "服务器已在后台运行，进程号: $PID"
        print_info "停止服务器命令: kill $PID"
    else
        print_error "服务器启动失败，请查看日志文件: server.log"
        rm -f server.pid
        return 1
    fi
}

# 主函数
main() {
    echo "========================================"
    echo "  HTTPS服务器一键启动脚本"
    echo "========================================"
    echo ""
    
    # 执行检查和自动安装
    check_and_install_nodejs || exit 1
    check_and_install_ffmpeg || exit 1
    check_ssl_cert || exit 1
    install_dependencies
    check_port
    
    echo ""
    
    # 启动服务器
    start_server
    printf "服务器已后台启动，按任意键继续..."
    read -n1 -s -r # -n1:读取1个字符；-s:不显示输入；-r:避免转义符问题echo "继续执行脚本！"
    echo -e "\n继续执行脚本！"

}

# 运行主函数
main