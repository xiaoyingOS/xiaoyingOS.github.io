========================================
Best Server Web 启动说明
========================================

项目概述
--------
本项目是一个高性能的 HTTP/HTTPS 服务器，支持文件上传、下载、WebSocket 等功能。

架构说明
--------
- HTTP 服务器：监听 8080 端口，处理 HTTP 请求
- HTTPS 代理：监听 8443 端口，处理 HTTPS 请求并转发到 HTTP 服务器

快速启动
--------

方法 1：使用启动脚本（推荐）
----------------------------

启动 HTTP 和 HTTPS 服务器：
    ./start_servers.sh 8080 8443 cert.pem key.pem

仅启动 HTTP 服务器：
    ./start_servers.sh 8080


方法 2：手动启动
------------------

1. 启动 HTTP 服务器：
    cd build
    nohup ./web_server > /dev/null 2>&1 & 推荐 重定向到空设备
    nohup ./web_server > ../server.log 2>&1 &

2. 启动 HTTPS 代理：
    nohup ./https_proxy 8443 cert.pem key.pem 8080 127.0.0.1 > https_proxy.log 2>&1 &


方法 3：启动守护进程版本
----------------------------

cd build
nohup ./web_server_daemon > ../server.log 2>&1 & 一般不需要


服务器状态检查
--------------

检查 HTTP 服务器：
    curl http://localhost:8080/api/status

检查 HTTPS 服务器：
    curl -k https://localhost:8443/api/status

检查进程状态：
    ps aux | grep -E "web_server|https_proxy"


访问地址
--------

HTTP：
    http://localhost:8080
    http://localhost:8080/api/status
    http://localhost:8080/api/files

HTTPS（使用 -k 忽略证书验证）：
    https://localhost:8443
    https://localhost:8443/api/status
    https://localhost:8443/api/files


API 接口
--------

POST  /upload              - 上传单个文件
POST  /upload/folder       - 上传文件夹
GET   /list/folder/*       - 列出文件夹内容
GET   /download/folder/*   - 获取文件夹文件列表
GET   /files/*             - 下载文件（支持断点续传）
GET   /api/files           - 列出所有文件
GET   /api/status          - 服务器状态
GET   /api/logs            - 服务器日志


日志文件
--------

HTTP 服务器日志：./server.log
HTTPS 代理日志：./https_proxy.log

查看日志：
    tail -f server.log
    tail -f https_proxy.log


停止服务器
------------

使用 Ctrl+C（如果前台运行）

或手动停止：
    pkill -f web_server
    pkill -f https_proxy


证书说明
--------

当前使用的是自签名证书，仅用于开发和测试。

生成自签名证书：
    openssl req -x509 -newkey rsa:2048 -keyout key.pem -out cert.pem -days 365 -nodes -subj "/CN=localhost"


目录结构
--------

web/
├── build/              # 构建输出目录
│   ├── web_server      # HTTP 服务器可执行文件
│   └── web_server_daemon  # 守护进程版本
├── static/             # 静态文件目录
├── uploads/            # 上传文件目录
├── cert.pem            # SSL 证书
├── key.pem             # SSL 私钥
├── https_proxy         # HTTPS 代理程序
├── start_servers.sh    # 启动脚本
├── start_https.sh      # HTTPS 启动脚本
├── server.log          # 服务器日志
└── https_proxy.log     # HTTPS 代理日志


性能说明
--------

- HTTP 响应时间：< 0.01 秒
- HTTPS 响应时间：约 0.01 秒
- 支持高并发连接
- 支持断点续传
- 支持文件上传


故障排除
------------

1. 端口被占用：
    检查端口：netstat -tlnp | grep 8080
    停止进程：pkill -f web_server

2. HTTPS 连接失败：
    检查证书文件是否存在
    检查 HTTPS 代理是否运行
    查看日志：cat https_proxy.log

3. 文件上传失败：
    检查 uploads 目录权限
    检查磁盘空间
    查看服务器日志


安全注意事项
------------

1. 自签名证书仅用于开发和测试
2. 生产环境应使用 CA 签名的证书
3. 定期更新证书
4. 配置防火墙规则
5. 使用强密码保护敏感功能


技术支持
------------

文档文件：
    HTTPS_README.md - HTTPS 详细说明
    HTTPS_USAGE.md  - HTTPS 使用指南
    README.md       - 项目说明

日志文件：
    server.log      - 服务器运行日志
    https_proxy.log - HTTPS 代理日志


========================================
启动说明结束
========================================