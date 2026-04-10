#!/bin/bash

# 启动简单的 HTTP 服务器

cd /data/data/com.termux/files/home/Server/web/build

# 停止已存在的服务器
kill $(ps aux | grep simple_http | grep -v grep | awk '{print $2}') 2>/dev/null

# 启动服务器
./simple_http

echo "Server stopped"