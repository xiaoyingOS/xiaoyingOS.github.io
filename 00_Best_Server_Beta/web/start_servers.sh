#!/bin/bash

# 启动HTTP和HTTPS服务器的脚本

if [ -z "$1" ]; then
    echo "Usage: $0 <http_port> [https_port] [cert_file] [key_file]"
    echo "Example: $0 8080 8443 cert.pem key.pem"
    echo ""
    echo "If only http_port is specified, only HTTP server will be started."
    exit 1
fi

HTTP_PORT=$1
HTTPS_PORT=${2:-8443}
CERT_FILE=${3:-cert.pem}
KEY_FILE=${4:-key_file}

# 停止已运行的服务器
echo "Stopping existing servers..."
pkill -f "web_server --port $HTTP_PORT" 2>/dev/null
pkill -f "https_proxy $HTTPS_PORT" 2>/dev/null
sleep 1

# 检查证书文件
if [ ! -f "$CERT_FILE" ]; then
    echo "Error: Certificate file not found: $CERT_FILE"
    echo "Generating self-signed certificate..."
    openssl req -x509 -newkey rsa:2048 -keyout "$KEY_FILE" -out "$CERT_FILE" -days 365 -nodes -subj "/CN=localhost"
fi

if [ ! -f "$KEY_FILE" ]; then
    echo "Error: Key file not found: $KEY_FILE"
    exit 1
fi

# 启动HTTP服务器
echo "Starting HTTP server on port $HTTP_PORT..."
./build/web_server --port $HTTP_PORT > ~/http_server.log 2>&1 &
HTTP_PID=$!
echo "HTTP server started with PID: $HTTP_PID"

# 等待HTTP服务器启动
sleep 2

# 检查HTTP服务器是否成功启动
if ! kill -0 $HTTP_PID 2>/dev/null; then
    echo "Error: HTTP server failed to start. Check ~/http_server.log for details."
    exit 1
fi

# 检查是否需要启动HTTPS代理
if [ -n "$HTTPS_PORT" ]; then
    echo "Starting HTTPS proxy on port $HTTPS_PORT..."
    ./build/https_proxy $HTTPS_PORT $CERT_FILE $KEY_FILE $HTTP_PORT 127.0.0.1 > ~/https_proxy.log 2>&1 &
    HTTPS_PID=$!
    echo "HTTPS proxy started with PID: $HTTPS_PID"
    
    # 等待HTTPS代理启动
    sleep 1
    
    # 检查HTTPS代理是否成功启动
    if ! kill -0 $HTTPS_PID 2>/dev/null; then
        echo "Warning: HTTPS proxy failed to start. Check ~/https_proxy.log for details."
        HTTPS_PID=""
    fi
fi

echo ""
echo "========================================="
echo "Servers are running!"
echo "========================================="
echo "HTTP server:  http://127.0.0.1:$HTTP_PORT"

if [ -n "$HTTPS_PID" ]; then
    echo "HTTPS proxy: https://127.0.0.1:$HTTPS_PORT"
fi

echo ""
echo "Log files:"
echo "  HTTP server: ~/http_server.log"
if [ -n "$HTTPS_PID" ]; then
    echo "  HTTPS proxy: ~/https_proxy.log"
fi
echo ""
echo "Press Ctrl+C to stop all servers"
echo "========================================="

# 保存PID到文件
echo $HTTP_PID > ~/http_server.pid
if [ -n "$HTTPS_PID" ]; then
    echo $HTTPS_PID > ~/https_proxy.pid
fi

# 等待用户中断
trap "echo ''; echo 'Stopping servers...'; kill $HTTP_PID 2>/dev/null; [ -n '$HTTPS_PID' ] && kill $HTTPS_PID 2>/dev/null; rm -f ~/http_server.pid ~/https_proxy.pid; echo 'Servers stopped'; exit 0" INT TERM

# 保持脚本运行
wait