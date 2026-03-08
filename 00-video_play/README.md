# 视频分割音频转换服务器

基于Node.js + FFmpeg的HTTPS视频分割和音频转换服务器。

## 功能特性

- 🎬 视频分割和转换
- 🎵 多种音频格式支持（AAC、MP3、Vorbis、PCM、FLAC）
- 🔄 批量文件处理
- 📁 文件管理（上传、下载、删除）
- ⚙️ FFmpeg完整配置（视频编码器、质量控制、帧率、分辨率等）
- 🌙/☀️ 自动主题切换（08:00-18:00浅色主题，18:00-08:00深色主题）
- ✨ 炫酷的关闭按钮动画效果

## 系统要求

- Node.js (推荐 v14+)
- npm
- FFmpeg
- SSL证书（cert.pem 和 key.pem）

## 快速开始

### 方式一：使用一键启动脚本（推荐）

#### Linux / macOS / Android Termux

```bash
# 启动服务器
./start.sh

# 停止服务器
./stop.sh
```

#### Windows

```batch
# 启动服务器
start.bat

# 停止服务器
stop.bat
```

### 方式二：手动启动

```bash
# 安装依赖
npm install

# 启动服务器
node https-server.js
```

## 访问地址

启动成功后，在浏览器中访问：

```
https://localhost:8691/test-split-manual.html
```

⚠️ **注意**：首次访问需要信任自签名证书

- 点击"高级" → "继续访问"或"接受风险"

## 脚本说明

### start.sh / start.bat

一键启动脚本会自动执行以下操作：

1. ✅ 检查Node.js和npm是否安装
2. ✅ 检查SSL证书文件是否存在
3. ✅ 自动安装依赖（如果未安装）
4. ✅ 检查并释放被占用的端口
5. ✅ 启动HTTPS服务器
6. ✅ 显示访问地址和进程信息

### stop.sh / stop.bat

停止服务器脚本会：

1. 🔍 查找运行中的服务器进程
2. 🛑 安全停止所有相关进程
3. 📋 显示停止结果

## FFmpeg配置

服务器支持完整的FFmpeg参数配置：

### 视频编码器
- copy（复制）
- libx264（H.264）
- libx265（H.265/HEVC）
- libvpx-vp9（VP9）
- libaom-av1（AV1）

### 质量控制
- CRF值：0-51（0=无损，23=默认）
- 编码预设：ultrafast ~ veryslow

### 帧率
- 24fps（电影）
- 25fps（PAL）
- 30fps（NTSC）
- 60fps（高帧率）
- 120fps（超高帧率）

### 分辨率
- QVGA (320x240)
- VGA (640x480)
- 480p (854x480)
- 720p (1280x720)
- 1080p (1920x1080)
- 2K (2560x1440)
- 4K (3840x2160)

### 音频格式
- AAC（192kbps）
- MP3（192kbps）
- Vorbis（192kbps）
- PCM无损
- FLAC无损

## 主题切换

系统根据时间段自动切换主题：

- **08:00 - 18:00**：浅色主题（☀️）
- **18:00 - 08:00**：深色主题（🌙）

用户也可以手动点击主题按钮切换。

## 文件结构

```
video_play/
├── cert.pem              # SSL证书文件
├── key.pem               # SSL密钥文件
├── https-server.js       # HTTPS服务器主文件
├── package.json          # 项目配置
├── package-lock.json     # 依赖锁定文件
├── test-split-manual.html # 前端界面
├── start.sh              # Linux/macOS/Termux启动脚本
├── stop.sh               # Linux/macOS/Termux停止脚本
├── start.bat             # Windows启动脚本
├── stop.bat              # Windows停止脚本
├── node_modules/         # 依赖目录
├── output/               # 输出文件目录
└── temp/                 # 临时文件目录
```

## 端口配置

默认端口：`8691`

如需修改端口，编辑 `https-server.js` 文件：

```javascript
const PORT = 8691; // 修改为你想要的端口号
```

## 日志查看

服务器运行日志保存在 `server.log` 文件中：

```bash
# 实时查看日志
tail -f server.log
```

## 故障排除

### 端口被占用

如果启动时提示端口被占用，启动脚本会自动停止占用端口的进程。

手动操作：

```bash
# 查找占用端口的进程
lsof -ti:8691

# 停止进程
kill -9 <PID>
```

### SSL证书问题

确保以下文件存在：
- `cert.pem`
- `key.pem`

如果没有SSL证书，可以使用以下命令生成自签名证书：

```bash
openssl req -x509 -newkey rsa:4096 -keyout key.pem -out cert.pem -days 365 -nodes
```

### 依赖安装失败

尝试清除缓存重新安装：

```bash
rm -rf node_modules package-lock.json
npm install
```

## 许可证

MIT License