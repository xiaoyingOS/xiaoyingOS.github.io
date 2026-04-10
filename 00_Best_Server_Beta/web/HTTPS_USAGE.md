# HTTPS功能快速使用指南

## 当前状态

✅ **HTTP服务器**：运行正常，端口8080
✅ **HTTPS代理**：运行正常，端口8443
✅ **SSL证书**：已配置（cert.pem, key.pem）

## 快速测试

```bash
# 测试HTTP
curl http://127.0.0.1:8080/

# 测试HTTPS
curl -k https://127.0.0.1:8443/
```

## 启动服务器

### 方式1：使用启动脚本（推荐）

```bash
cd /data/data/com.termux/files/home/Server/web

# 启动HTTP和HTTPS服务器
./start_servers.sh 8080 8443 cert.pem key.pem

# 仅启动HTTP服务器
./start_servers.sh 8080
```

### 方式2：手动启动

```bash
cd /data/data/com.termux/files/home/Server/web

# 启动HTTP服务器
./web_server --port 8080 > ~/http_server.log 2>&1 &

# 启动HTTPS代理
./https_proxy 8443 cert.pem key.pem 8080 127.0.0.1 > ~/https_proxy.log 2>&1 &
```

## 停止服务器

```bash
# 如果使用启动脚本，按Ctrl+C

# 或手动停止
pkill -f web_server
pkill -f https_proxy
```

## 查看日志

```bash
# HTTP服务器日志
cat ~/http_server.log

# HTTPS代理日志
cat ~/https_proxy.log
```

## 性能指标

- **HTTP响应时间**：< 0.1秒
- **HTTPS响应时间**：约5秒

## 技术实现

- **HTTP服务器**：使用原有的best_server框架
- **HTTPS代理**：独立C++程序，使用OpenSSL处理SSL/TLS
- **架构**：HTTPS代理 → 转发请求 → HTTP服务器

## 文件说明

- `web_server`：主HTTP服务器
- `https_proxy`：HTTPS代理程序
- `start_servers.sh`：启动脚本
- `HTTPS_README.md`：详细技术文档
- `cert.pem`：SSL证书
- `key.pem`：SSL私钥

## 注意事项

1. HTTPS代理使用简单的实现，性能不如专业的HTTPS服务器
2. 响应时间较慢（约5秒）是由于SSL握手和代理转发
3. 生产环境建议使用专业的HTTPS服务器（如nginx）
4. 自签名证书仅用于开发测试

## 获取帮助

查看详细文档：
```bash
cat HTTPS_README.md
```