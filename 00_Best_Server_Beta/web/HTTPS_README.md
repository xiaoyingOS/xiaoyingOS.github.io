# HTTPS 支持说明

## 概述

本服务器现在支持HTTP和HTTPS两种连接方式。HTTPS功能通过一个独立的HTTPS代理实现，该代理使用OpenSSL处理SSL/TLS加密，并将解密后的请求转发到HTTP服务器。

## 架构

```
客户端 (HTTPS) → HTTPS代理 (8443) → HTTP服务器 (8080)
```

- **HTTPS代理**：监听HTTPS端口，处理SSL/TLS握手，解密请求
- **HTTP服务器**：处理解密后的HTTP请求并返回响应

## 快速开始

### 方法1：使用启动脚本（推荐）

```bash
# 启动HTTP和HTTPS服务器
./start_servers.sh 8080 8443 cert.pem key.pem

# 仅启动HTTP服务器
./start_servers.sh 8080
```

### 方法2：手动启动

```bash
# 1. 启动HTTP服务器
./web_server --port 8080 > ~/http_server.log 2>&1 &

# 2. 启动HTTPS代理
./https_proxy 8443 cert.pem key.pem 8080 127.0.0.1 > ~/https_proxy.log 2>&1 &
```

### 方法3：使用HTTPS参数启动（当前不可用）

```bash
# 注意：此功能当前不可用，因为SSL socket生命周期管理问题
# ./web_server --https --port 8443 --cert cert.pem --key key.pem
```

## 生成自签名证书

如果没有SSL证书，可以使用以下命令生成自签名证书：

```bash
openssl req -x509 -newkey rsa:2048 -keyout key.pem -out cert.pem -days 365 -nodes -subj "/CN=localhost"
```

## 测试

### 测试HTTP连接

```bash
curl http://127.0.0.1:8080/
```

### 测试HTTPS连接

```bash
curl -k https://127.0.0.1:8443/
```

注意：`-k`参数表示忽略证书验证（因为使用的是自签名证书）。

## 性能说明

- **HTTP响应时间**：< 0.1秒
- **HTTPS响应时间**：约5秒（由于SSL握手和代理转发）

HTTPS响应时间较慢是由于：
1. SSL握手需要额外的网络往返
2. HTTPS代理使用简单的实现，没有优化性能
3. HTTP连接使用了5秒超时设置

如果需要更好的性能，可以考虑：
1. 使用专业的反向代理（如nginx）
2. 优化HTTPS代理实现
3. 使用连接池和缓存

## 停止服务器

```bash
# 使用Ctrl+C（如果使用启动脚本）

# 或手动停止
pkill -f web_server
pkill -f https_proxy
```

## 日志文件

- HTTP服务器日志：`~/http_server.log`
- HTTPS代理日志：`~/https_proxy.log`

## 文件说明

- `web_server`：主HTTP服务器程序
- `https_proxy`：HTTPS代理程序（转发HTTPS请求到HTTP服务器）
- `start_servers.sh`：启动脚本（推荐使用）
- `cert.pem`：SSL证书文件
- `key.pem`：SSL私钥文件

## 技术细节

### HTTPS代理实现

HTTPS代理是一个独立的C++程序，使用以下技术：
- OpenSSL SSL/TLS库
- 多线程处理客户端连接
- TCP socket通信
- 简单的请求转发逻辑

### SSL/TLS支持

- TLS版本：支持TLS 1.0, 1.1, 1.2, 1.3
- 加密算法：OpenSSL默认算法
- 证书：支持PEM格式的X.509证书

### 安全注意事项

1. **自签名证书**：仅用于开发和测试，生产环境应使用CA签名的证书
2. **密码算法**：确保使用强加密算法
3. **证书有效期**：定期更新证书
4. **防火墙**：正确配置防火墙规则

## 故障排除

### HTTPS连接失败

1. 检查证书和密钥文件是否存在
2. 检查端口是否被占用
3. 查看HTTPS代理日志：`cat ~/https_proxy.log`
4. 检查HTTP服务器是否正在运行

### 性能问题

1. 如果HTTPS响应时间过长，可以调整超时设置
2. 考虑使用专业的HTTPS服务器（如nginx）
3. 优化网络连接

## 未来改进

1. 性能优化（连接池、缓存等）
2. 支持HTTP/2
3. 更好的日志和监控
4. 配置文件支持
5. 集成到主服务器（解决SSL socket生命周期问题）

## 许可证

本HTTPS实现遵循与主服务器相同的许可证。