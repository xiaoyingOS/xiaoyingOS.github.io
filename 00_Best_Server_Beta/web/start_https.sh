#!/bin/bash

# HTTPS服务器启动脚本
# 使用openssl s_server作为HTTPS代理，转发到HTTP服务器

if [ $# -lt 2 ]; then
    echo "Usage: $0 <https_port> <http_port> [cert_file] [key_file]"
    echo "Example: $0 8443 8080 cert.pem key.pem"
    exit 1
fi

HTTPS_PORT=$1
HTTP_PORT=$2
CERT_FILE=${3:-cert.pem}
KEY_FILE=${4:-key.pem}

# 检查证书和密钥文件
if [ ! -f "$CERT_FILE" ]; then
    echo "Error: Certificate file not found: $CERT_FILE"
    exit 1
fi

if [ ! -f "$KEY_FILE" ]; then
    echo "Error: Key file not found: $KEY_FILE"
    exit 1
fi

# 启动HTTP服务器
echo "Starting HTTP server on port $HTTP_PORT..."
./web_server --port $HTTP_PORT > ~/http_server.log 2>&1 &
HTTP_PID=$!
echo "HTTP server started with PID: $HTTP_PID"

# 等待HTTP服务器启动
sleep 2

# 启动HTTPS服务器
echo "Starting HTTPS server on port $HTTPS_PORT..."
openssl s_server \
    -accept $HTTPS_PORT \
    -cert $CERT_FILE \
    -key $KEY_FILE \
    -www \
    -quiet \
    -HTTP > ~/https_server.log 2>&1 &
HTTPS_PID=$!
echo "HTTPS server started with PID: $HTTPS_PID"

echo ""
echo "========================================="
echo "HTTPS server is running!"
echo "========================================="
echo "HTTP server: http://127.0.0.1:$HTTP_PORT"
echo "HTTPS server: https://127.0.0.1:$HTTPS_PORT"
echo ""
echo "Press Ctrl+C to stop all servers"
echo "========================================="

# 等待用户中断
trap "echo ''; echo 'Stopping servers...'; kill $HTTP_PID $HTTPS_PID 2>/dev/null; echo 'Servers stopped'; exit 0" INT TERM

# 保持脚本运行
wait
