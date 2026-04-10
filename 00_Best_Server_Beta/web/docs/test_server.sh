#!/bin/bash
cd /data/data/com.termux/files/home/Server/web

# 清理旧进程
pkill -9 -f simple_upload_server 2>/dev/null

# 清理旧日志
rm -f server_test.log

# 启动服务器
./simple_upload_server > server_test.log 2>&1 &
SERVER_PID=$!

echo "Server PID: $SERVER_PID"
sleep 2

# 检查服务器是否启动
if ps -p $SERVER_PID > /dev/null 2>&1; then
    echo "Server is running"

    # 测试GET请求
    echo ""
    echo "Testing GET /api/files..."
    curl -v http://127.0.0.1:8080/api/files 2>&1 | head -20

    # 测试POST上传
    echo ""
    echo "Testing POST /upload..."
    echo "Hello World" | curl -v -X POST http://127.0.0.1:8080/upload -H "X-Filename: test.txt" --data-binary @- 2>&1 | head -20

    # 查看服务器日志
    echo ""
    echo "Server log:"
    cat server_test.log

    # 停止服务器
    kill $SERVER_PID 2>/dev/null
else
    echo "Server failed to start"
    cat server_test.log
fi