# Best Server 项目构建说明书

## 项目概述
这是一个高性能HTTP服务器项目，支持文件上传/下载、WebSocket通信、HTTP/2协议等功能。

## 编译成功的关键修复

### 1. 禁用LTO优化
**问题**：LTO优化导致静态库中的某些函数符号被内联，导致链接时找不到符号（如HTTPResponse::enable_stream_file、HTTPRequest::get_range等）。

**解决**：
- 修改根目录 CMakeLists.txt，将 `option(ENABLE_LTO "Enable Link-Time Optimization" ON)` 改为 `OFF`

### 2. 编译顺序和依赖关系
**正确的编译顺序**：
```bash
# 1. 编译静态库 libbest_server.a
cd /data/data/com.termux/files/home/Server
cmake .
make clean
make -j4

# 2. 复制静态库到build目录
mkdir -p build
cp libbest_server.a build/

# 3. 编译web服务器
cd web
make clean
make -j4
```

### 3. 依赖文件说明
- libbest_server.a：核心静态库，包含所有网络、IO、核心功能
- build/libbest_server.a：web服务器链接时使用的静态库副本
- web/CMakeLists.txt：使用 `../build/libbest_server.a` 链接静态库

## 代码修复（已应用）

### 修复1：大文件下载超时问题
**位置**：src/network/http_server.cpp 第771行
**代码**：
```cpp
auto send_next = [this, conn, remote_addr](size_t bytes_sent) {
    auto& state = conn->file_stream_state();
    if (state.active) {
        // 更新活动时间，避免传输中的连接被超时断开
        conn->update_activity_time();
        
        state.bytes_sent += bytes_sent;
        // ... 其余代码
    }
};
```
**说明**：每次发送数据块时更新活动时间，避免文件传输时连接被60秒超时断开。

### 修复2：文件描述符泄漏问题
**位置**：src/network/http_server.cpp 第43行
**代码**：
```cpp
void HTTPConnection::close() {
    // 清理文件流状态，避免文件描述符泄漏
    reset_file_stream_state();
    
    if (socket_) {
        socket_->close();
        socket_.reset();
    }
}
```
**说明**：连接关闭时清理文件流状态，避免文件描述符泄漏导致服务器崩溃。

## 完整构建步骤

### 首次编译
```bash
cd /data/data/com.termux/files/home/Server

# 1. 配置项目（已禁用LTO）
cmake .

# 2. 编译静态库
make clean
make best_server -j4

# 3. 创建build目录并复制静态库
mkdir -p build
cp libbest_server.a build/

# 4. 编译web服务器
cd web
make clean
make -j4
```

### 增量编译（代码修改后）
```bash
# 如果修改了 src/ 下的代码
cd /data/data/comtermux/files/home/Server
make -j4
cp libbest_server.a build/

# 如果修改了 web/ 下的代码
cd web
make -j4
```

### 清理和完全重新编译
```bash
# 清理所有编译产物
rm -rf CMakeFiles build
rm -rf web/CMakeFiles

# 重新配置和编译
cmake .
make clean
make -j4

# 编译web服务器
mkdir -p build
cp libbest_server.a build/
cd web
make clean
make -j4
```

## 配置说明

### 关键配置文件
- 根目录 CMakeLists.txt：定义ENABLE_LTO选项和编译目标
- web/CMakeLists.txt：定义web_server链接配置
- web/server.cpp：HTTP服务器主程序

### 当前配置（修复后）
- LTO：禁用（避免符号丢失）
- 线程支持：pthread
- HTTP/2：启用
- WebSocket：启用
- 最大请求大小：1GB（需要修复整数溢出）

### 编译警告（需要注意）
- server.cpp第515-516行：整数溢出警告（5 * 1024 * 1024 * 1024 实际只有1GB，需要使用5ULL）
- server.cpp第909行：未使用的参数警告

## 服务器运行

### 启动服务器
```bash
cd /data/data/com.termux/files/home/Server/web
./web_server

# 或后台运行
./web_server > server.log 2>&1 &
```

### 停止服务器
```bash
pkill -f web_server
```

### 访问服务器
- 地址：http://0.0.0.0:8080
- API：/api/status

## 已知问题

1. **整数溢出**：5GB配置实际只有1GB，需要使用ULL后缀
2. **编译警告**：有未使用的参数警告
3. **日志文件**：web_server.log可能非常大（6.8GB），需要定期清理

## 项目结构
```
Server/
├── include/           # 头文件
├── src/              # 源代码
│   ├── core/         # 核心功能
│   ├── network/      # 网络功能
│   ├── io/           # IO操作
│   └── ...
├── web/              # Web服务器
│   ├── server.cpp    # 主程序
│   ├── static/       # 静态文件
│   └── uploads/      # 上传目录
├── build/            # 构建目录（存放libbest_server.a）
└── libbest_server.a  # 静态库
```

## 总结

**编译成功的关键**：
1. 禁用LTO优化（避免符号丢失）
2. 正确的编译顺序（先编译libbest_server.a，再编译web_server）
3. 正确的静态库位置（web/CMakeLists.txt使用../build/libbest_server.a）

**已修复的bug**：
1. 大文件下载超时问题（添加update_activity_time）
2. 文件描述符泄漏（添加reset_file_stream_state）