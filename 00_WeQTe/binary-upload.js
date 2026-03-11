// 临时文件写入器类 - 用于流式处理大文件（内存传输方式）
class TempFileWriter {
    constructor(fileId, fileName, totalChunks) {
        this.fileId = fileId;
        this.fileName = fileName;
        this.totalChunks = totalChunks;
        this.chunks = new Map(); // 临时存储chunks
        this.receivedChunks = new Set();
        console.log(`📂 【初始化】临时文件写入器创建: ${fileId}`);
    }
    
    async writeChunk(chunkIndex, chunkData) {
        console.log(`💾 【写入】准备写入chunk ${chunkIndex}, 大小: ${(chunkData.length / 1024 / 1024).toFixed(1)}MB`);
        
        try {
            // 模拟写入临时存储（实际应用中可以写入IndexedDB或服务器临时文件）
            this.chunks.set(chunkIndex, chunkData);
            this.receivedChunks.add(chunkIndex);
            
            console.log(`✅ 【成功】chunk ${chunkIndex} 已写入临时存储`);
            console.log(`📊 【存储状态】当前存储chunks数量: ${this.chunks.size}`);
            
            return true;
        } catch (error) {
            console.error(`❌ 【失败】chunk ${chunkIndex} 写入失败:`, error);
            return false;
        }
    }
    
    async assembleFile() {
        console.log(`🔧 【组装】开始组装文件: ${this.fileName}`);
        console.log(`📋 【清单】需要组装 ${this.totalChunks} 个chunks`);
        
        try {
            // 验证所有chunks都已接收
            for (let i = 0; i < this.totalChunks; i++) {
                if (!this.chunks.has(i)) {
                    throw new Error(`缺少chunk ${i}`);
                }
                console.log(`✅ 【找到】chunk ${i}`);
            }
            
            console.log(`🔄 【合并】开始合并 ${this.totalChunks} 个chunks...`);
            
            // 计算总大小
            let totalSize = 0;
            for (let i = 0; i < this.totalChunks; i++) {
                totalSize += this.chunks.get(i).length;
            }
            
            const mergedArray = new Uint8Array(totalSize);
            let offset = 0;
            
            // 按顺序合并chunks
            for (let i = 0; i < this.totalChunks; i++) {
                const chunkData = this.chunks.get(i);
                mergedArray.set(chunkData, offset);
                offset += chunkData.length;
                
                if ((i + 1) % 5 === 0 || i === this.totalChunks - 1) {
                    console.log(`📊 【进度】已合并 ${i + 1}/${this.totalChunks} chunks`);
                }
            }
            
            console.log(`🎉 【完成】文件组装成功: ${this.fileName}`);
            console.log(`📏 【大小】最终文件大小: ${(totalSize / 1024 / 1024).toFixed(1)}MB`);
            
            // 创建Blob URL
            const blob = new Blob([mergedArray], { type: 'application/octet-stream' });
            const url = URL.createObjectURL(blob);
            
            // 清理临时存储
            this.cleanup();
            
            return {
                url: url,
                size: totalSize,
                blob: blob
            };
            
        } catch (error) {
            console.error(`❌ 【失败】文件组装失败:`, error);
            this.cleanup();
            throw error;
        }
    }
    
    cleanup() {
        console.log(`🗑️ 【清理】临时存储已清理`);
        this.chunks.clear();
        this.receivedChunks.clear();
    }
}

// 流式文件写入器类 - 使用Directory Handle API直接写入硬盘
class StreamFileWriter {
    constructor(fileId, fileName, totalChunks, folderName = null, relativePath = null) {
        this.fileId = fileId;
        this.fileName = fileName;
        this.totalChunks = totalChunks;
        this.folderName = folderName;
        this.relativePath = relativePath;
        this.receivedChunks = new Set();
        this.currentOffset = 0;
        this.directoryHandle = null;
        this.fileHandle = null;
        this.writable = null;
        this.initialized = false;
        console.log(`🌊 【初始化】流式文件写入器创建: ${fileId}, 文件夹: ${folderName}, 相对路径: ${relativePath}`);
    }
    
    async initialize() {
        try {
            if (this.initialized) {
                console.log(`🌊 【初始化】流式写入器已初始化: ${this.fileName}`);
                return true;
            }
            
            // 检查是否支持Directory Handle API
            if (!('showDirectoryPicker' in window)) {
                throw new Error('浏览器不支持Directory Handle API');
            }
            
            // 使用用户预先选择的文件夹
            if (!window.streamDirectoryHandle) {
                throw new Error('请先选择文件保存文件夹');
            }
            
            this.directoryHandle = window.streamDirectoryHandle;
            
            // 从根目录开始，创建目标目录
            let targetDirectory = this.directoryHandle;
            
            // 如果有文件夹名，创建根目录文件夹
            if (this.folderName) {
                console.log(`🌊 【创建根目录文件夹】${this.folderName}`);
                targetDirectory = await targetDirectory.getDirectoryHandle(this.folderName, { create: true });
            }
            
            // 如果有相对路径，需要创建子文件夹
            if (this.relativePath) {
                // 解析相对路径
                const pathParts = this.relativePath.split('/');
                
                // 如果有根文件夹名，跳过它；否则不跳过
                let startIndex = 0;
                if (this.folderName && pathParts[0] === this.folderName) {
                    startIndex = 1;
                }
                
                // 提取子文件夹路径（排除最后一个元素，因为它是文件名）
                const folderPath = pathParts.slice(startIndex, -1);
                
                // 逐级创建子文件夹
                for (const folderName of folderPath) {
                    console.log(`🌊 【创建子文件夹】${folderName}`);
                    targetDirectory = await targetDirectory.getDirectoryHandle(folderName, { create: true });
                }
            }
            
            // 在目标目录中创建或获取文件
            console.log(`🌊 【初始化】正在创建文件: ${this.fileName}`);
            this.fileHandle = await targetDirectory.getFileHandle(this.fileName, { create: true });
            
            // 创建可写流
            this.writable = await this.fileHandle.createWritable();
            this.initialized = true;
            
            console.log(`✅ 【初始化】流式写入器已初始化: ${this.fileName}`);
            return true;
        } catch (error) {
            console.error(`❌ 【初始化】流式写入器初始化失败:`, error);
            throw error;
        }
    }
    
    async writeChunk(chunkIndex, chunkData) {
        console.log(`🌊 【流式写入】chunk ${chunkIndex}/${this.totalChunks}, 大小: ${(chunkData.length / 1024 / 1024).toFixed(1)}MB`);
        
        try {
            // 直接写入chunk到文件流
            const uint8Array = new Uint8Array(chunkData);
            await this.writable.write(uint8Array);
            this.currentOffset += uint8Array.length;
            this.receivedChunks.add(chunkIndex);
            
            console.log(`✅ 【流式写入】chunk ${chunkIndex} 已写入硬盘, 当前偏移: ${this.currentOffset}`);
            
            return true;
        } catch (error) {
            console.error(`❌ 【流式写入】chunk ${chunkIndex} 写入失败:`, error);
            return false;
        }
    }
    
    async finalize() {
        console.log(`🌊 【完成】关闭文件流: ${this.fileName}`);
        
        try {
            // 关闭文件流
            await this.writable.close();
            
            // 获取文件信息
            const file = await this.fileHandle.getFile();
            
            console.log(`✅ 【流式写入】文件保存成功: ${this.fileName}`);
            console.log(`📏 【大小】文件大小: ${(file.size / 1024 / 1024).toFixed(1)}MB`);
            console.log(`💾 【保存位置】已保存到用户选择的文件夹`);
            
            // 流式写入模式下，不创建Blob URL，因为文件已经保存到硬盘
            return {
                url: null,
                size: file.size,
                savedToDisk: true
            };
        } catch (error) {
            console.error(`❌ 【流式写入】文件流关闭失败:`, error);
            throw error;
        }
    }
    
    cleanup() {
        console.log(`🗑️ 【清理】流式写入器已清理`);
        this.receivedChunks.clear();
        this.directoryHandle = null;
        this.fileHandle = null;
        this.writable = null;
        this.currentOffset = 0;
        this.initialized = false;
    }
}

// 文件图标获取函数
function getFileIcon(fileName) {
    if (!fileName) return '📎';
    
    const ext = fileName.split('.').pop().toLowerCase();
    
    const iconMap = {
        // 图片
        'jpg': '🖼️', 'jpeg': '🖼️', 'png': '🖼️', 'gif': '🖼️', 'webp': '🖼️', 'bmp': '🖼️', 'svg': '🖼️',
        // 视频
        'mp4': '🎬', 'avi': '🎬', 'mov': '🎬', 'wmv': '🎬', 'flv': '🎬', 'mkv': '🎬', 'webm': '🎬',
        // 音频
        'mp3': '🎵', 'wav': '🎵', 'ogg': '🎵', 'flac': '🎵', 'aac': '🎵', 'm4a': '🎵',
        // 文档
        'pdf': '📄', 'doc': '📝', 'docx': '📝', 'xls': '📊', 'xlsx': '📊', 'ppt': '📊', 'pptx': '📊',
        'txt': '📃', 'rtf': '📃', 'md': '📃',
        // 压缩文件
        'zip': '📦', 'rar': '📦', '7z': '📦', 'tar': '📦', 'gz': '📦',
        // 代码
        'js': '💻', 'html': '💻', 'css': '💻', 'json': '💻', 'xml': '💻', 'py': '💻', 'java': '💻',
        'c': '💻', 'cpp': '💻', 'h': '💻', 'php': '💻', 'rb': '💻', 'go': '💻', 'rs': '💻',
        // 其他
        'exe': '⚙️', 'dmg': '⚙️', 'apk': '📱', 'ipa': '📱'
    };
    
    return iconMap[ext] || '📎';
}

// 使用chat.html中的全局变量
window.uploadFileWithBinaryChunks = async function uploadFileWithBinaryChunks(file, options = {}) {
    if (!appState.connectedUser) {
        showNotification('请先连接到用户', 'error');
        return;
    }
    
    // 检查是否是文件夹传输
    const isFolderTransfer = !!file.folderName;
    const folderName = file.folderName || null;
    
    // 如果是文件夹传输，不要在这里更新进度
    // 进度更新应该在文件完成发送时进行
    if (isFolderTransfer) {
        console.log(`📁 开始发送文件夹文件: ${file.name}, 文件夹: ${folderName}`);
        
        const folderTransfer = window.folderTransfers.get(folderName);
        if (folderTransfer) {
            console.log(`📁 文件夹传输状态: ${folderTransfer.sentFiles}/${folderTransfer.totalFiles} 个文件`);
        }
    }
    
    // 检查并发上传限制（单个文件上传才限制）
    if (!isFolderTransfer) {
        const activeUploads = Array.from(window.activeTransfers?.values() || [])
            .filter(transfer => transfer.type === 'upload' && !transfer.cancelled);
        
        if (activeUploads.length >= 3) {
            showNotification('同时上传文件数量不能超过3个', 'error');
            return;
        }
    }
    
    const fileId = await chunkUploader.calculateFileId(file);
    const chunks = await chunkUploader.splitFile(file);
    
    // 自动检测文件类型
    const fileType = file.type || '';
    const isVideoFile = fileType.startsWith('video/');
    const isImageFile = fileType.startsWith('image/');
    
    // 为当前文件创建独立的传输状态
    const transferState = {
        fileId: fileId,
        fileName: file.name,
        fileSize: file.size,
        isCancelled: false,
        sentChunks: new Set(),
        file: file, // 保存文件对象引用
        isVideo: options.isVideo || isVideoFile, // 如果没有明确指定，根据文件类型自动判断
        isImage: isImageFile, // 添加isImage标记
        shouldDestroy: options.shouldDestroy || false,
        messageId: Date.now().toString(), // 生成统一的消息ID
        isFolderTransfer: isFolderTransfer, // 标记是否是文件夹传输
        folderName: folderName // 文件夹名称
    };

    // 保存到fileTransferMap以便后续重发使用
    if (!window.fileTransferMap) {
        window.fileTransferMap = new Map();
    }
    window.fileTransferMap.set(fileId, {
        fileId: fileId,
        name: file.name,
        size: file.size,
        totalChunks: chunks.length,
        chunks: new Map(),
        receivedChunks: new Set(),
        file: file, // 保存文件对象引用
        isVideo: transferState.isVideo,
        isImage: transferState.isImage,
        shouldDestroy: transferState.shouldDestroy,
        messageId: transferState.messageId,
        isFolderTransfer: isFolderTransfer,
        folderName: folderName
    });
    
    try {
        // 只有非文件夹传输才显示单个文件上传通知
        if (!isFolderTransfer) {
            showNotification(`开始上传文件: ${file.name}`, 'info');
        }
        
        // 如果是视频文件且不是文件夹传输，立即在发送方显示视频消息（边传输边观看）
        if (transferState.isVideo && !isFolderTransfer) {
            await displayVideoMessageForSender(file, transferState.shouldDestroy, transferState.messageId);
        }
        // 如果是图片文件且不是文件夹传输，立即在发送方显示图片预览
        else if (transferState.isImage && !isFolderTransfer) {
            await displayImageMessageForSender(file, transferState.shouldDestroy, transferState.messageId);
        }
        
        // 添加到活跃传输列表（单个文件上传才需要）
        if (!isFolderTransfer && typeof activeTransfers !== 'undefined') {
            activeTransfers.set(fileId, {
                type: 'upload',
                cancelled: false,
                fileName: file.name,
                isVideo: transferState.isVideo,
                isImage: transferState.isImage,
                shouldDestroy: transferState.shouldDestroy
            });
        }
        
        // 显示上传进度条（单个文件上传才显示）
        if (!isFolderTransfer) {
            showUploadChunkTransferStatus(file.name, 0, chunks.length);
        } else {
            // 文件夹传输：显示文件夹进度，按文件数量
            const folderTransfer = window.folderTransfers.get(folderName);
            if (folderTransfer) {
                // 显示当前进度（已发送文件数 / 总文件数）
                showUploadChunkTransferStatus(
                    file.name, 
                    folderTransfer.sentFiles, 
                    folderTransfer.totalFiles, 
                    true, 
                    folderName
                );
            }
        }
        
        // 使用单发送器模式，确保发送-接收同步
        console.log(`使用单发送器模式传输文件: ${transferState.fileName}${isFolderTransfer ? ' (文件夹传输)' : ''}`);
        await startFileSender(transferState, chunks);
        
    } catch (error) {
        console.error('文件上传失败:', error);
        if (!isFolderTransfer) {
            showNotification(`文件上传失败: ${error.message}`, 'error');
        }
    }
};

// 为发送方显示视频消息
async function displayVideoMessageForSender(file, shouldDestroy, messageId) {
    const container = document.getElementById('messagesContainer');
    if (!container) return;
    
    // 创建本地视频URL
    const url = URL.createObjectURL(file);
    
    // 使用传递过来的messageId（如果没有则生成新的）
    if (!messageId) {
        messageId = Date.now().toString();
    }
    
    // 创建消息元素
    const messageElement = document.createElement('div');
    messageElement.className = 'message sent';
    messageElement.dataset.messageId = messageId;
    
    // 创建内容元素
    const contentElement = document.createElement('div');
    contentElement.className = 'message-content';
    
    // 显示视频播放器
    const videoElement = document.createElement('video');
    videoElement.className = 'video-message';
    videoElement.src = url;
    videoElement.controls = true;
    videoElement.controlsList = ''; // 确保不隐藏任何控件
    videoElement.style.maxWidth = '240px'; // 与接收方保持一致
    videoElement.style.maxHeight = '400px'; // 与接收方保持一致
    videoElement.style.minHeight = 'auto';
    videoElement.style.borderRadius = '8px';
    videoElement.style.marginBottom = '10px';
    videoElement.style.display = 'block';
    videoElement.style.objectFit = 'contain';
    
    // 添加响应式样式：电脑端显示更大
    const styleElement = document.createElement('style');
    styleElement.textContent = `
        @media (min-width: 768px) {
            [data-message-id="${messageId}"] .video-message {
                max-width: 430px !important;
            }
        }
    `;
    contentElement.appendChild(styleElement);
    
    // 创建视频信息容器
    const videoInfo = document.createElement('div');
    videoInfo.className = 'video-info';
    
    // 视频文件名
    const fileNameElement = document.createElement('div');
    fileNameElement.className = 'file-name';
    fileNameElement.textContent = file.name;
    
    // 视频大小
    const fileSizeElement = document.createElement('div');
    fileSizeElement.className = 'file-size';
    fileSizeElement.textContent = `${(file.size / 1024 / 1024).toFixed(2)}MB`;
    
    // 视频分辨率（动态获取）
    const resolutionElement = document.createElement('div');
    resolutionElement.className = 'file-resolution';
    resolutionElement.textContent = '分辨率: 加载中...';
    resolutionElement.style.fontSize = '12px';
    resolutionElement.style.color = 'rgba(255, 255, 255, 0.7)';
    resolutionElement.style.marginTop = '2px';
    
    // 传输状态标记
    const statusLabel = document.createElement('div');
    statusLabel.className = 'sent-label';
    statusLabel.id = `video-status-${messageId}`; // 添加ID以便后续更新
    statusLabel.textContent = '⏳ 传输中...';
    statusLabel.style.color = '#FF9800';
    statusLabel.style.fontSize = '12px';
    statusLabel.style.marginTop = '5px';
    
    videoInfo.appendChild(fileNameElement);
    videoInfo.appendChild(fileSizeElement);
    videoInfo.appendChild(resolutionElement);
    videoInfo.appendChild(statusLabel);
    
    // 监听视频元数据加载，获取分辨率
    videoElement.addEventListener('loadedmetadata', function() {
        const width = videoElement.videoWidth;
        const height = videoElement.videoHeight;
        if (width > 0 && height > 0) {
            resolutionElement.textContent = `分辨率: ${width}×${height}`;
        } else {
            resolutionElement.textContent = '分辨率: 未知';
        }
    });
    
    // 组装视频消息
    contentElement.appendChild(videoElement);
    contentElement.appendChild(videoInfo);
    
    // 添加时间戳到contentElement
    const timeElement = document.createElement('div');
    timeElement.className = 'message-time';
    timeElement.textContent = new Date().toLocaleTimeString();
    timeElement.style.display = 'block';
    timeElement.style.textAlign = 'right';
    timeElement.style.clear = 'both';
    timeElement.style.marginTop = '8px';
    timeElement.style.width = '100%';
    timeElement.style.boxSizing = 'border-box';
    contentElement.appendChild(timeElement);
    
    // 组装消息
    messageElement.appendChild(contentElement);
    
    // 添加长按事件
    if (typeof addLongPressEvent === 'function') {
        const messageObj = {
            id: messageId,
            type: 'video',
            fileName: file.name
        };
        addLongPressEvent(messageElement, messageObj);
    }
    
    // 添加到消息历史记录，用于撤回功能
    if (typeof appState !== 'undefined' && appState.messageHistory) {
        appState.messageHistory.push({
            id: messageId,
            type: 'video',
            fileName: file.name,
            fileSize: file.size,
            sender: appState.currentUser.id,
            timestamp: new Date(),
            isFlash: false,
            destroyTime: shouldDestroy ? new Date().getTime() + 13000 : null
        });
    }
    
    // 添加到容器
    container.appendChild(messageElement);
    
    // 滚动到底部
    container.scrollTop = container.scrollHeight;
    
    // 如果需要自动销毁
    if (shouldDestroy) {
        setTimeout(() => {
            messageElement.style.opacity = '0';
            setTimeout(() => {
                messageElement.remove();
            }, 300);
        }, 13000);
    }
}

// 为发送方显示图片消息
async function displayImageMessageForSender(file, shouldDestroy, messageId) {
    const container = document.getElementById('messagesContainer');
    if (!container) return;
    
    // 创建本地图片URL
    const url = URL.createObjectURL(file);
    
    // 使用传递过来的messageId（如果没有则生成新的）
    if (!messageId) {
        messageId = Date.now().toString();
    }
    
    // 创建消息元素
    const messageElement = document.createElement('div');
    messageElement.className = 'message sent';
    messageElement.dataset.messageId = messageId;
    
    // 创建内容元素
    const contentElement = document.createElement('div');
    contentElement.className = 'message-content';
    
    // 显示图片
    const imageElement = document.createElement('img');
    imageElement.className = 'image-message';
    imageElement.src = url;
    imageElement.alt = file.name;
    imageElement.style.maxWidth = '200px';
    imageElement.style.maxHeight = '200px';
    imageElement.style.borderRadius = '8px';
    imageElement.style.marginBottom = '10px';
    imageElement.style.display = 'block';
    imageElement.style.cursor = 'pointer';
    
    // 点击图片查看大图
    imageElement.onclick = () => {
        const fullscreenContainer = document.createElement('div');
        fullscreenContainer.style.cssText = `
            position: fixed;
            top: 0;
            left: 0;
            width: 100%;
            height: 100%;
            background: rgba(0, 0, 0, 0.9);
            z-index: 10000;
            display: flex;
            align-items: center;
            justify-content: center;
        `;
        
        const fullImage = document.createElement('img');
        fullImage.src = url;
        fullImage.style.cssText = `
            max-width: 90%;
            max-height: 90%;
            object-fit: contain;
            border-radius: 8px;
        `;
        
        fullscreenContainer.appendChild(fullImage);
        document.body.appendChild(fullscreenContainer);
        
        fullscreenContainer.onclick = () => {
            if (fullscreenContainer && fullscreenContainer.parentNode === document.body) {
                document.body.removeChild(fullscreenContainer);
            }
        };
    };
    
    // 创建图片信息容器
    const imageInfo = document.createElement('div');
    imageInfo.className = 'image-info';
    
    // 图片文件名
    const fileNameElement = document.createElement('div');
    fileNameElement.className = 'file-name';
    fileNameElement.textContent = file.name;
    
    // 图片大小
    const fileSizeElement = document.createElement('div');
    fileSizeElement.className = 'file-size';
    fileSizeElement.textContent = `${(file.size / 1024 / 1024).toFixed(2)}MB`;
    
    // 传输状态标记
    const statusLabel = document.createElement('div');
    statusLabel.className = 'sent-label';
    statusLabel.id = `image-status-${messageId}`; // 添加ID以便后续更新
    statusLabel.textContent = '⏳ 传输中...';
    statusLabel.style.color = '#FF9800';
    statusLabel.style.fontSize = '12px';
    statusLabel.style.marginTop = '5px';
    
    imageInfo.appendChild(fileNameElement);
    imageInfo.appendChild(fileSizeElement);
    imageInfo.appendChild(statusLabel);
    
    // 组装图片消息
    contentElement.appendChild(imageElement);
    contentElement.appendChild(imageInfo);
    
    // 添加时间戳到contentElement
    const timeElement = document.createElement('div');
    timeElement.className = 'message-time';
    timeElement.textContent = new Date().toLocaleTimeString();
    timeElement.style.display = 'block';
    timeElement.style.textAlign = 'right';
    timeElement.style.clear = 'both';
    timeElement.style.marginTop = '8px';
    timeElement.style.width = '100%';
    timeElement.style.boxSizing = 'border-box';
    contentElement.appendChild(timeElement);
    
    // 组装消息
    messageElement.appendChild(contentElement);
    
    // 添加长按事件
    if (typeof addLongPressEvent === 'function') {
        const messageObj = {
            id: messageId,
            type: 'image',
            fileName: file.name
        };
        addLongPressEvent(messageElement, messageObj);
    }
    
    // 添加到消息历史记录，用于撤回功能
    if (typeof appState !== 'undefined' && appState.messageHistory) {
        appState.messageHistory.push({
            id: messageId,
            type: 'image',
            fileName: file.name,
            fileSize: file.size,
            sender: appState.currentUser.id,
            timestamp: new Date(),
            isFlash: false,
            destroyTime: shouldDestroy ? new Date().getTime() + 13000 : null
        });
    }
    
    // 添加到容器
    container.appendChild(messageElement);
    
    // 滚动到底部
    container.scrollTop = container.scrollHeight;
    
    // 如果需要自动销毁
    if (shouldDestroy) {
        setTimeout(() => {
            messageElement.style.opacity = '0';
            setTimeout(() => {
                messageElement.remove();
            }, 300);
        }, 13000);
    }
}

// 单发送器函数 - 确保严格的发送-接收同步
async function startFileSender(transferState, chunks) {
    console.log(`启动单发送器 for ${transferState.fileName}`);
    
    try {
        for (let chunkIndex = 0; chunkIndex < chunks.length; chunkIndex++) {
            // 检查取消状态
            if (transferState.isCancelled) {
                console.log(`文件 ${transferState.fileName} 已取消，发送器停止`);

                // 更新视频状态标记为已取消
                if (transferState.isVideo && transferState.messageId) {
                    const statusLabel = document.getElementById(`video-status-${transferState.messageId}`);
                    if (statusLabel) {
                        statusLabel.textContent = '✗ 已取消';
                        statusLabel.style.color = '#d13438';
                    }
                }
                // 更新图片状态标记为已取消
                else if (transferState.isImage && transferState.messageId) {
                    const statusLabel = document.getElementById(`image-status-${transferState.messageId}`);
                    if (statusLabel) {
                        statusLabel.textContent = '✗ 已取消';
                        statusLabel.style.color = '#d13438';
                    }
                }

                break;
            }

            // 检查暂停状态（只有非文件夹传输才需要检查暂停）
            if (!transferState.isFolderTransfer) {
                while (true) {
                    const activeTransfer = activeTransfers?.get(transferState.fileId);
                    if (!activeTransfer || activeTransfer.cancelled) {
                        console.log(`文件 ${transferState.fileName} 已取消，发送器停止`);

                        // 更新视频状态标记为已取消
                        if (transferState.isVideo && transferState.messageId) {
                            const statusLabel = document.getElementById(`video-status-${transferState.messageId}`);
                            if (statusLabel) {
                                statusLabel.textContent = '✗ 已取消';
                                statusLabel.style.color = '#d13438';
                            }
                        }
                        // 更新图片状态标记为已取消
                        else if (transferState.isImage && transferState.messageId) {
                            const statusLabel = document.getElementById(`image-status-${transferState.messageId}`);
                            if (statusLabel) {
                                statusLabel.textContent = '✗ 已取消';
                                statusLabel.style.color = '#d13438';
                            }
                        }

                        return;
                    }
                    if (!activeTransfer.paused) {
                        break;
                    }
                    // 暂停中，等待100ms后再次检查
                    await new Promise(resolve => setTimeout(resolve, 100));
                }
            } else {
                // 文件夹传输时，直接检查取消状态
                if (transferState.isCancelled) {
                    console.log(`文件夹文件 ${transferState.fileName} 已取消，发送器停止`);
                    return;
                }
            }

            try {
                console.log(`发送 chunk ${chunkIndex}/${chunks.length}`);
                
                // 处理chunk
                const chunk = chunks[chunkIndex];
                const chunkBlob = chunk.data;
                const arrayBuffer = await chunkBlob.arrayBuffer();
                const uint8Array = new Uint8Array(arrayBuffer);
                const chunkHash = await chunkUploader.calculateChunkHash(arrayBuffer);
                
                // 发送chunk消息
                const message = {
                    type: 'binaryChunk',
                    data: {
                        fileId: transferState.fileId,
                        fileName: transferState.fileName,
                        fileSize: transferState.fileSize,
                        chunkIndex: chunkIndex,
                        totalChunks: chunks.length,
                        chunkData: Array.from(uint8Array),
                        chunkHash: chunkHash,
                        senderId: appState.currentUser.id,
                        targetUserId: appState.connectedUser.id,
                        folderName: transferState.file.folderName || null,
                        folderId: transferState.file.folderId || null,
                        relativePath: transferState.file.webkitRelativePath || transferState.file.relativePath || null,
                        isVideo: transferState.isVideo || false,
                        isImage: transferState.isImage || false,
                        shouldDestroy: transferState.shouldDestroy || false,
                        messageId: transferState.messageId || null // 添加messageId用于撤回
                    }
                };
                
                console.log(`📤 发送chunk ${chunkIndex}, 文件: ${transferState.fileName}, 文件夹: ${transferState.file.folderName}`);
                                        console.log(`🔍 调试发送：transferState.file.webkitRelativePath =`, transferState.file.webkitRelativePath);
                                        console.log(`🔍 调试发送：transferState.file.relativePath =`, transferState.file.relativePath);
                                        sendServerMessage(message);                console.log(`已发送 chunk ${chunkIndex}`);
                
                // 更新进度（只有非文件夹传输才更新单个文件进度条）
                if (!transferState.isFolderTransfer) {
                    updateUploadChunkProgress(chunkIndex + 1, chunks.length);
                } else {
                    // 文件夹传输：更新文件夹进度，按文件数量
                    const folderTransfer = window.folderTransfers.get(transferState.folderName);
                    if (folderTransfer) {
                        updateUploadChunkProgress(
                            folderTransfer.sentFiles, 
                            folderTransfer.totalFiles, 
                            true, 
                            transferState.folderName
                        );
                    }
                }
                
                // 等待接收方确认
                await waitForChunkAck(transferState.fileId, chunkIndex);
                console.log(`收到 chunk ${chunkIndex} 确认，继续发送下一个`);
                
            } catch (error) {
                console.error(`chunk ${chunkIndex} 发送失败:`, error);
                break;
            }
        }
        
        console.log(`单发送器完成`);
        
        // 发送完成消息
        const completeMessage = {
            type: 'uploadComplete',
            data: {
                fileId: transferState.fileId,
                fileName: transferState.fileName,
                fileSize: transferState.fileSize,
                totalChunks: chunks.length,
                senderId: appState.currentUser.id,
                targetUserId: appState.connectedUser.id,
                folderName: transferState.file.folderName || null,
                folderId: transferState.file.folderId || null,
                relativePath: transferState.file.webkitRelativePath || transferState.file.relativePath || null,
                messageId: transferState.messageId || null, // 添加messageId用于撤回
                isVideo: transferState.isVideo || false,
                isImage: transferState.isImage || false,
                shouldDestroy: transferState.shouldDestroy || false
            }
        };
        
        sendServerMessage(completeMessage);
        
        // 文件夹传输：更新文件夹进度
        if (transferState.isFolderTransfer) {
            const folderTransfer = window.folderTransfers.get(transferState.folderName);
            if (folderTransfer) {
                folderTransfer.sentFiles++;
                console.log(`📁 文件 ${transferState.fileName} 发送完成, 文件夹进度: ${folderTransfer.sentFiles}/${folderTransfer.totalFiles}`);
                
                // 更新发送方文件夹进度显示
                if (typeof updateFolderSenderProgress === 'function') {
                    updateFolderSenderProgress(transferState.folderName, folderTransfer.sentFiles);
                }
                
                // 更新外部进度条
                updateUploadChunkProgress(folderTransfer.sentFiles, folderTransfer.totalFiles, true, transferState.folderName);
                
                // 检查是否是文件夹中的最后文件
                if (folderTransfer.sentFiles >= folderTransfer.totalFiles) {
                    showNotification(`文件夹 ${transferState.folderName} 发送完成`, 'success');
                    // 确保进度条达到100%
                    updateUploadChunkProgress(folderTransfer.totalFiles, folderTransfer.totalFiles, true, transferState.folderName);
                }
            }
        }
        
        // 只有非文件夹传输才显示单个文件完成通知
        if (!transferState.isFolderTransfer) {
            showNotification(`文件 ${transferState.fileName} 上传完成`, 'success');
        }
        
        // 如果是视频文件且不是文件夹传输，更新状态标记
        if (transferState.isVideo && transferState.messageId && !transferState.isFolderTransfer) {
            const statusLabel = document.getElementById(`video-status-${transferState.messageId}`);
            if (statusLabel) {
                statusLabel.textContent = '✓ 已发送';
                statusLabel.style.color = '#4CAF50';
            }
        }
        // 如果是图片文件且不是文件夹传输，更新状态标记
        else if (transferState.isImage && transferState.messageId && !transferState.isFolderTransfer) {
            const statusLabel = document.getElementById(`image-status-${transferState.messageId}`);
            if (statusLabel) {
                statusLabel.textContent = '✓ 已发送';
                statusLabel.style.color = '#4CAF50';
            }
        }
        
        // 清理状态（只有非文件夹传输才清理）
        if (!transferState.isFolderTransfer && typeof activeTransfers !== 'undefined') {
            activeTransfers.delete(transferState.fileId);
        }
        
        // 隐藏进度条
        if (!transferState.isFolderTransfer) {
            document.getElementById("uploadChunkTransferStatus").classList.remove("show");
        } else {
            // 文件夹传输：所有文件发送完成后立即隐藏进度条
            const folderTransfer = window.folderTransfers.get(transferState.folderName);
            if (folderTransfer && folderTransfer.sentFiles >= folderTransfer.totalFiles) {
                document.getElementById("uploadChunkTransferStatus").classList.remove("show");
            }
        }
        
    } catch (error) {
        console.error('发送器失败:', error);
        throw error;
    }
}

// chunk确认等待函数
function waitForChunkAck(fileId, chunkIndex) {
    return new Promise((resolve) => {
        const timeout = setTimeout(() => {
            console.log(`chunk ${chunkIndex} 确认超时，继续发送下一个`);
            resolve();
        }, 5000); // 5秒超时

        // 监听确认消息
        const originalHandleServerMessage = window.handleServerMessage;
        window.handleServerMessage = function(message) {
            if (message.type === 'chunkAck' &&
                message.data.fileId === fileId &&
                message.data.chunkIndex === chunkIndex) {
                clearTimeout(timeout);
                console.log(`收到 chunk ${chunkIndex} 确认`);
                resolve();
            }

            // 调用原始处理函数
            originalHandleServerMessage(message);
        };
    });
}

// 处理重发chunk请求
window.handleResendChunksRequest = async function(fileId, missingChunks, requestUserId) {
    console.log(`处理重发chunk请求: ${fileId}, 缺失:`, missingChunks);

    // 查找对应的传输状态
    const transfer = window.fileTransferMap?.get(fileId);
    if (!transfer) {
        console.error('找不到传输状态:', fileId);
        return;
    }

    // 重新读取文件数据
    const file = transfer.file;
    if (!file) {
        console.error('找不到文件数据:', fileId);
        return;
    }

    // 重新分割文件
    const chunks = await chunkUploader.splitFile(file);

    // 逐个发送缺失的chunk
    for (const chunkIndex of missingChunks) {
        if (chunkIndex >= chunks.length) {
            console.error(`chunk索引超出范围: ${chunkIndex}`);
            continue;
        }

        try {
            const chunk = chunks[chunkIndex];
            const chunkBlob = chunk.data;
            const arrayBuffer = await chunkBlob.arrayBuffer();
            const uint8Array = new Uint8Array(arrayBuffer);
            const chunkHash = await chunkUploader.calculateChunkHash(arrayBuffer);

            // 发送重发的chunk
            const message = {
                type: 'retransmittedChunk',
                data: {
                    fileId: fileId,
                    fileName: file.name,
                    fileSize: file.size,
                    chunkIndex: chunkIndex,
                    totalChunks: chunks.length,
                    chunkData: Array.from(uint8Array),
                    chunkHash: chunkHash,
                    senderId: appState.currentUser.id,
                    targetUserId: requestUserId
                }
            };

            sendServerMessage(message);
            console.log(`已重发chunk ${chunkIndex}`);

            // 短暂延迟，避免消息拥堵
            await new Promise(resolve => setTimeout(resolve, 50));
        } catch (error) {
            console.error(`重发chunk ${chunkIndex} 失败:`, error);
        }
    }

    console.log(`所有缺失chunk已重发完成`);
    showNotification(`已重发 ${missingChunks.length} 个丢失的数据块`, 'success');
};

// 文件合并函数 - 支持临时文件写入器
window.mergeBinaryChunks = async function mergeBinaryChunks(fileId) {
    console.log('开始合并二进制chunks:', fileId);
    
    const transfer = window.fileTransferMap.get(fileId);
    if (!transfer) {
        console.error('找不到传输状态:', fileId);
        return;
    }
    
    console.log(`📁 文件信息: ${transfer.name}, 文件夹: ${transfer.folderName}, 相对路径: ${transfer.relativePath}, messageId: ${transfer.messageId}`);
    
    try {
        // 检查chunks是否存在
        if (!transfer.chunks || transfer.chunks.size === 0) {
            console.error('找不到chunks数据');
            return;
        }
        
        console.log('开始合并chunks');
        
        // 计算总大小
        let totalSize = 0;
        for (let i = 0; i < transfer.totalChunks; i++) {
            const chunkData = transfer.chunks.get(i);
            if (chunkData) {
                totalSize += chunkData.length;
            }
        }
        
        console.log(`总大小: ${(totalSize / 1024 / 1024).toFixed(2)}MB`);
        
        const mergedArray = new Uint8Array(totalSize);
        let offset = 0;
        
        // 按顺序合并chunks
        for (let i = 0; i < transfer.totalChunks; i++) {
            const chunkData = transfer.chunks.get(i);
            if (chunkData) {
                mergedArray.set(chunkData, offset);
                offset += chunkData.length;
                console.log(`合并chunk ${i}/${transfer.totalChunks}, 当前offset: ${offset}`);
            } else {
                console.error(`缺少chunk ${i}`);
                return;
            }
        }
        
        console.log('chunks合并完成');
        
        // 检测文件类型
        let mimeType = 'application/octet-stream';
        const fileName = transfer.name.toLowerCase();
        const isVideoFile = fileName.endsWith('.mp4') || fileName.endsWith('.webm') || fileName.endsWith('.ogg') || fileName.endsWith('.mov') || fileName.endsWith('.avi') || fileName.endsWith('.mkv') || transfer.isVideo;
        const isImageFile = fileName.endsWith('.jpg') || fileName.endsWith('.jpeg') || fileName.endsWith('.png') || fileName.endsWith('.gif') || fileName.endsWith('.webp') || fileName.endsWith('.bmp') || fileName.endsWith('.svg') || transfer.isImage;
        
        if (isVideoFile) {
            if (fileName.endsWith('.mp4')) {
                mimeType = 'video/mp4';
            } else if (fileName.endsWith('.webm')) {
                mimeType = 'video/webm';
            } else if (fileName.endsWith('.ogg')) {
                mimeType = 'video/ogg';
            } else if (fileName.endsWith('.mov')) {
                mimeType = 'video/quicktime';
            } else if (fileName.endsWith('.avi')) {
                mimeType = 'video/x-msvideo';
            } else if (fileName.endsWith('.mkv')) {
                mimeType = 'video/x-matroska';
            } else {
                mimeType = 'video/mp4'; // 默认视频类型
            }
            console.log(`检测到视频文件，MIME类型: ${mimeType}`);
        } else if (isImageFile) {
            if (fileName.endsWith('.jpg') || fileName.endsWith('.jpeg')) {
                mimeType = 'image/jpeg';
            } else if (fileName.endsWith('.png')) {
                mimeType = 'image/png';
            } else if (fileName.endsWith('.gif')) {
                mimeType = 'image/gif';
            } else if (fileName.endsWith('.webp')) {
                mimeType = 'image/webp';
            } else if (fileName.endsWith('.bmp')) {
                mimeType = 'image/bmp';
            } else if (fileName.endsWith('.svg')) {
                mimeType = 'image/svg+xml';
            } else {
                mimeType = 'image/jpeg'; // 默认图片类型
            }
            console.log(`检测到图片文件，MIME类型: ${mimeType}`);
        }
        
        // 创建Blob URL
        const blob = new Blob([mergedArray], { type: mimeType });
        const url = URL.createObjectURL(blob);
        
        console.log(`创建Blob URL: ${url}, MIME类型: ${mimeType}`);
        
            console.log(`🔍 调试：folderName=${transfer.folderName}, window.folderTransfers=${!!window.folderTransfers}, has=${window.folderTransfers?.has(transfer.folderName)}`);
            if (transfer.folderName && window.folderTransfers && window.folderTransfers.has(transfer.folderName)) {
                console.log(`✅ 文件属于文件夹，添加到文件夹列表`);
            } else {
                console.log(`❌ 文件不属于文件夹，将单独显示`);
            }
        // 创建下载链接
        const downloadLink = document.createElement('a');
        downloadLink.href = url;
        downloadLink.download = transfer.name;
        downloadLink.textContent = `下载 ${transfer.name} (${(transfer.size / 1024 / 1024).toFixed(1)}MB)`;
        downloadLink.style.display = 'block';
        downloadLink.style.margin = '10px 0';
        downloadLink.style.padding = '10px';
        downloadLink.style.backgroundColor = '#4CAF50';
        downloadLink.style.color = 'white';
        downloadLink.style.textDecoration = 'none';
        downloadLink.style.borderRadius = '5px';
        
        // 检查是否是文件夹中的文件
        if (transfer.folderName && window.folderTransfers && window.folderTransfers.has(transfer.folderName)) {
            const folderTransfer = window.folderTransfers.get(transfer.folderName);
            
            // 添加到已接收文件列表
            folderTransfer.receivedFiles.push({
                name: transfer.name,
                size: transfer.size,
                relativePath: transfer.relativePath,
                blob: blob
            });
            
            console.log(`文件夹 ${transfer.folderName} 已接收 ${folderTransfer.receivedFiles.length}/${folderTransfer.totalFiles} 个文件`);
            
            // 检查是否所有文件都接收完成
            if (folderTransfer.receivedFiles.length === folderTransfer.totalFiles) {
                showNotification(`文件夹 ${transfer.folderName} 接收完成`, 'success');
                // 更新文件夹消息状态
                updateFolderMessageStatus(transfer.folderName, true);
            }
            
            // 不显示单独的文件消息，只显示文件夹消息
            return;
        } else {
            // 不是文件夹文件，正常显示
            setTimeout(() => {
                const container = document.getElementById('messagesContainer');
                if (container) {
                    // 检测是否是视频文件
                    const fileName = transfer.name.toLowerCase();
                    const isVideoFile = fileName.endsWith('.mp4') || fileName.endsWith('.webm') || fileName.endsWith('.ogg') || fileName.endsWith('.mov') || fileName.endsWith('.avi') || fileName.endsWith('.mkv') || transfer.isVideo;
                    
                    // 使用发送方传递过来的messageId，如果没有则生成新的
                    const messageId = transfer.messageId || Date.now().toString();
                    console.log(`接收方使用messageId: ${messageId}`);
                    
                    // 创建消息元素
                    const messageElement = document.createElement('div');
                    messageElement.className = 'message received';
                    messageElement.dataset.messageId = messageId;
                    
                    // 创建内容元素
                    const contentElement = document.createElement('div');
                    contentElement.className = 'message-content';
                    
                    if (isVideoFile) {
                        // 创建视频和按钮的容器（水平布局）
                        const videoContainer = document.createElement('div');
                        videoContainer.style.display = 'flex';
                        videoContainer.style.alignItems = 'center';
                        videoContainer.style.gap = '5px';
                        videoContainer.style.maxWidth = '100%';
                        videoContainer.style.overflow = 'visible';
                        
                        // 显示视频播放器
                        const videoElement = document.createElement('video');
                        videoElement.className = 'video-message';
                        videoElement.src = url;
                        videoElement.controls = true;
                        videoElement.controlsList = ''; // 确保不隐藏任何控件
                        videoElement.style.maxWidth = '240px'; // 手机端默认
                        videoElement.style.maxHeight = '400px';
                        videoElement.style.minHeight = 'auto';
                        videoElement.style.borderRadius = '8px';
                        videoElement.style.flex = '0 0 auto';
                        videoElement.style.display = 'block';
                        videoElement.style.objectFit = 'contain';
                        
                        // 添加响应式样式：电脑端显示更大
                        const styleElement = document.createElement('style');
                        styleElement.textContent = `
                            @media (min-width: 768px) {
                                [data-message-id="${messageId}"] .video-message {
                                    max-width: 430px !important;
                                }
                            }
                        `;
                        contentElement.appendChild(styleElement);
                        
                        // 下载按钮
                        const downloadButton = document.createElement('button');
                        downloadButton.className = 'download-btn';
                        downloadButton.textContent = '⬇';
                        downloadButton.style.flexShrink = '0';
                        downloadButton.style.width = '32px';
                        downloadButton.style.height = '32px';
                        downloadButton.style.minWidth = '32px';
                        downloadButton.style.minHeight = '32px';
                        downloadButton.onclick = () => {
                            const link = document.createElement('a');
                            link.href = url;
                            link.download = transfer.name;
                            document.body.appendChild(link);
                            link.click();
                            document.body.removeChild(link);
                        };
                        
                        // 组装视频容器
                        videoContainer.appendChild(videoElement);
                        videoContainer.appendChild(downloadButton);
                        
                        // 创建视频信息容器
                        const videoInfo = document.createElement('div');
                        videoInfo.className = 'video-info';
                        
                        // 视频文件名
                        const fileNameElement = document.createElement('div');
                        fileNameElement.className = 'file-name';
                        fileNameElement.textContent = transfer.name;
                        
                        // 视频大小
                        const fileSizeElement = document.createElement('div');
                        fileSizeElement.className = 'file-size';
                        fileSizeElement.textContent = formatFileSize ? formatFileSize(transfer.size) : `${(transfer.size / 1024 / 1024).toFixed(2)}MB`;
                        
                        // 视频分辨率（动态获取）
                        const resolutionElement = document.createElement('div');
                        resolutionElement.className = 'file-resolution';
                        resolutionElement.textContent = '分辨率: 加载中...';
                        resolutionElement.style.fontSize = '12px';
                        resolutionElement.style.color = 'rgba(0, 0, 0, 0.5)';
                        resolutionElement.style.marginTop = '2px';
                        
                        videoInfo.appendChild(fileNameElement);
                        videoInfo.appendChild(fileSizeElement);
                        videoInfo.appendChild(resolutionElement);
                        
                        // 组装视频消息
                        contentElement.appendChild(videoContainer);
                        contentElement.appendChild(videoInfo);
                        
                        // 监听视频元数据加载，获取分辨率
                        videoElement.addEventListener('loadedmetadata', function() {
                            const width = videoElement.videoWidth;
                            const height = videoElement.videoHeight;
                            if (width > 0 && height > 0) {
                                resolutionElement.textContent = `分辨率: ${width}×${height}`;
                            } else {
                                resolutionElement.textContent = '分辨率: 未知';
                            }
                        });
                        
                        // 如果需要自动销毁
                        if (transfer.shouldDestroy) {
                            showNotification('视频将在13秒后自动销毁', 'info');
                            setTimeout(() => {
                                messageElement.style.opacity = '0';
                                setTimeout(() => {
                                    messageElement.remove();
                                }, 300);
                            }, 13000);
                        }
                    } else if (isImageFile) {
                        // 创建图片和按钮的容器（水平布局）
                        const imageContainer = document.createElement('div');
                        imageContainer.style.display = 'flex';
                        imageContainer.style.alignItems = 'center';
                        imageContainer.style.gap = '10px';
                        
                        // 显示图片预览
                        const imageElement = document.createElement('img');
                        imageElement.className = 'image-message';
                        imageElement.src = url;
                        imageElement.style.maxWidth = '200px';
                        imageElement.style.maxHeight = '200px';
                        imageElement.style.borderRadius = '8px';
                        imageElement.style.flex = '1';
                        imageElement.style.display = 'block';
                        imageElement.style.cursor = 'pointer';
                        
                        // 点击图片查看大图
                        imageElement.onclick = () => {
                            const fullscreenContainer = document.createElement('div');
                            fullscreenContainer.style.cssText = `
                                position: fixed;
                                top: 0;
                                left: 0;
                                width: 100%;
                                height: 100%;
                                background: rgba(0, 0, 0, 0.9);
                                z-index: 10000;
                                display: flex;
                                align-items: center;
                                justify-content: center;
                            `;
                            
                            const fullImage = document.createElement('img');
                            fullImage.src = url;
                            fullImage.style.cssText = `
                                max-width: 90%;
                                max-height: 90%;
                                object-fit: contain;
                                border-radius: 8px;
                            `;
                            
                            fullscreenContainer.appendChild(fullImage);
                            document.body.appendChild(fullscreenContainer);
                            
                            fullscreenContainer.onclick = () => {
                                document.body.removeChild(fullscreenContainer);
                            };
                        };
                        
                        // 下载按钮
                        const downloadButton = document.createElement('button');
                        downloadButton.className = 'download-btn';
                        downloadButton.textContent = '⬇';
                        downloadButton.style.flexShrink = '0';
                        downloadButton.onclick = () => {
                            const link = document.createElement('a');
                            link.href = url;
                            link.download = transfer.name;
                            document.body.appendChild(link);
                            link.click();
                            document.body.removeChild(link);
                        };
                        
                        // 组装图片容器
                        imageContainer.appendChild(imageElement);
                        imageContainer.appendChild(downloadButton);
                        
                        // 创建图片信息容器
                        const imageInfo = document.createElement('div');
                        imageInfo.className = 'image-info';
                        
                        // 图片文件名
                        const fileNameElement = document.createElement('div');
                        fileNameElement.className = 'file-name';
                        fileNameElement.textContent = transfer.name;
                        
                        // 图片大小
                        const fileSizeElement = document.createElement('div');
                        fileSizeElement.className = 'file-size';
                        fileSizeElement.textContent = formatFileSize ? formatFileSize(transfer.size) : `${(transfer.size / 1024 / 1024).toFixed(2)}MB`;
                        
                        imageInfo.appendChild(fileNameElement);
                        imageInfo.appendChild(fileSizeElement);
                        
                        // 组装图片消息
                        contentElement.appendChild(imageContainer);
                        contentElement.appendChild(imageInfo);
                        
                        // 如果需要自动销毁
                        if (transfer.shouldDestroy) {
                            showNotification('图片将在13秒后自动销毁', 'info');
                            setTimeout(() => {
                                messageElement.style.opacity = '0';
                                setTimeout(() => {
                                    messageElement.remove();
                                }, 300);
                            }, 13000);
                        }
                    } else {
                        // 普通文件显示
                        const fileElement = document.createElement('div');
                        fileElement.className = 'file-message';
                        
                        // 文件图标
                        const fileIcon = document.createElement('div');
                        fileIcon.className = 'file-icon';
                        fileIcon.textContent = getFileIcon ? getFileIcon(transfer.name) : '📎';
                        
                        // 文件详情
                        const fileDetails = document.createElement('div');
                        fileDetails.className = 'file-details';
                        
                        // 文件名
                        const fileName = document.createElement('div');
                        fileName.className = 'file-name';
                        fileName.textContent = transfer.name;
                        
                        // 文件大小
                        const fileSize = document.createElement('div');
                        fileSize.className = 'file-size';
                        fileSize.textContent = formatFileSize ? formatFileSize(transfer.size) : `${(transfer.size / 1024 / 1024).toFixed(2)}MB`;
                        
                        fileDetails.appendChild(fileName);
                        fileDetails.appendChild(fileSize);
                        
                        // 下载按钮
                        const downloadButton = document.createElement('button');
                        downloadButton.className = 'download-btn';
                        downloadButton.textContent = '⬇';
                        downloadButton.onclick = () => {
                            const link = document.createElement('a');
                            link.href = url;
                            link.download = transfer.name;
                            document.body.appendChild(link);
                            link.click();
                            document.body.removeChild(link);
                        };
                        
                        // 组装文件消息
                        fileElement.appendChild(fileIcon);
                        fileElement.appendChild(fileDetails);
                        fileElement.appendChild(downloadButton);
                        
                        contentElement.appendChild(fileElement);
                    }
                    
                    // 添加时间戳
                    const timeElement = document.createElement('div');
                    timeElement.className = 'message-time';
                    timeElement.textContent = new Date().toLocaleTimeString();
                    timeElement.style.display = 'block';
                    timeElement.style.textAlign = 'right';
                    timeElement.style.clear = 'both';
                    timeElement.style.marginTop = '8px';
                    timeElement.style.width = '100%';
                    timeElement.style.boxSizing = 'border-box';
                    
                    // 组装消息
                    contentElement.appendChild(timeElement);
                    messageElement.appendChild(contentElement);
                    
                    // 添加长按事件
                    if (typeof addLongPressEvent === 'function') {
                        const messageObj = {
                            id: messageId,
                            type: isVideoFile ? 'video' : isImageFile ? 'image' : 'file',
                            fileName: transfer.name
                        };
                        addLongPressEvent(messageElement, messageObj);
                    }
                    
                    // 添加到消息历史记录，用于撤回功能
                    if (typeof appState !== 'undefined' && appState.messageHistory) {
                        appState.messageHistory.push({
                            id: messageId,
                            type: isVideoFile ? 'video' : isImageFile ? 'image' : 'file',
                            fileName: transfer.name,
                            fileSize: transfer.size,
                            sender: appState.connectedUser?.id || 'unknown',
                            timestamp: new Date(),
                            isFlash: false,
                            destroyTime: transfer.shouldDestroy ? new Date().getTime() + 13000 : null
                        });
                        console.log(`接收方${isVideoFile ? '视频' : isImageFile ? '图片' : '文件'}消息已添加到历史记录，ID: ${messageId}`);
                    }
                    
                    // 添加到容器
                    container.appendChild(messageElement);
                    
                    // 滚动到底部
                    container.scrollTop = container.scrollHeight;
                    
                    console.log(`文件消息已添加到聊天界面: ${transfer.name}`);
                } else {
                    console.error('找不到messagesContainer容器');
                }
            }, 100);
        }
        
        // 检查是否是文件夹中的文件
        if (transfer.folderName && window.folderTransfers && window.folderTransfers.has(transfer.folderName)) {
            const folderTransfer = window.folderTransfers.get(transfer.folderName);
            
            // 添加到已接收文件列表
            folderTransfer.receivedFiles.push({
                name: transfer.name,
                size: transfer.size,
                relativePath: transfer.relativePath,
                blob: blob
            });
            
            console.log(`文件夹 ${transfer.folderName} 已接收 ${folderTransfer.receivedFiles.length}/${folderTransfer.totalFiles} 个文件`);
            
            // 检查是否所有文件都接收完成
            if (folderTransfer.receivedFiles.length === folderTransfer.totalFiles) {
                showNotification(`文件夹 ${transfer.folderName} 接收完成`, 'success');
            }
        } else {
            showNotification(`文件 ${transfer.name} 接收完成`, 'success');
        }
        
    } catch (error) {
        console.error('合并chunks失败:', error);
        showNotification(`文件合并失败: ${error.message}`, 'error');
    } finally {
        // 清理传输状态
        window.fileTransferMap.delete(fileId);
        if (typeof activeTransfers !== 'undefined') {
            activeTransfers.delete(fileId);
        }
    }
};

// 处理文件拖拽，支持多文件延迟处理
window.handleFiles = function handleFiles(files) {
    console.log(`开始处理 ${files.length} 个文件`);
    
    // 转换为数组并按大小排序，小文件优先处理
    const fileArray = Array.from(files).sort((a, b) => a.size - b.size);
    
    // 检查是否有文件夹文件（通过relativePath判断）
    const folderFiles = new Map();
    const regularFiles = [];
    
    fileArray.forEach(file => {
        if (file.webkitRelativePath) {
            // 这是一个文件夹中的文件
            const folderName = file.webkitRelativePath.split('/')[0];
            if (!folderFiles.has(folderName)) {
                folderFiles.set(folderName, []);
            }
            folderFiles.get(folderName).push(file);
        } else {
            regularFiles.push(file);
        }
    });
    
    // 处理文件夹
    folderFiles.forEach((files, folderName) => {
        setTimeout(() => {
            console.log(`处理文件夹: ${folderName} (${files.length} 个文件)`);
            uploadFolder(folderName, files);
        }, 0);
    });
    
    // 处理单独文件
    regularFiles.forEach((file, index) => {
        setTimeout(() => {
            console.log(`处理文件 ${index + 1}/${regularFiles.length}: ${file.name}`);
            uploadFileWithBinaryChunks(file);
        }, (folderFiles.size + index) * 200); // 延迟处理
    });
};

// 上传文件夹
function uploadFolder(folderName, files) {
    console.log(`开始上传文件夹: ${folderName}, 包含 ${files.length} 个文件`);
    
    // 创建文件夹传输状态
    const folderId = `folder_${Date.now()}_${Math.random().toString(36).substr(2, 9)}`;
    
    // 存储文件夹传输信息
    if (!window.folderTransfers) {
        window.folderTransfers = new Map();
    }
    
    window.folderTransfers.set(folderName, {
        folderId: folderId,
        folderName: folderName,
        totalFiles: files.length,
        receivedFiles: [],
        files: files.map(file => ({
            name: file.name,
            size: file.size,
            relativePath: file.webkitRelativePath,
            fileId: null
        }))
    });
    
    // 发送文件夹信息给接收方
    const folderInfoMessage = {
        type: 'folderInfo',
        data: {
            folderId: folderId,
            folderName: folderName,
            totalFiles: files.length,
            files: files.map(file => ({
                name: file.name,
                size: file.size,
                relativePath: file.webkitRelativePath
            }))
        }
    };
    sendServerMessage(folderInfoMessage);
    
    // 逐个上传文件
    files.forEach((file, index) => {
        setTimeout(() => {
            console.log(`上传文件夹文件 ${index + 1}/${files.length}: ${file.webkitRelativePath}`);
            uploadFileInFolder(file, folderName, folderId);
        }, index * 300);
    });
}

// 在文件夹中上传文件
function uploadFileInFolder(file, folderName, folderId) {
    console.log(`🔍 uploadFileInFolder: file.webkitRelativePath =`, file.webkitRelativePath);
    console.log(`🔍 uploadFileInFolder: file.relativePath =`, file.relativePath);
    // 为文件添加文件夹信息
    file.folderName = folderName;
    file.folderId = folderId;
    console.log(`📁 设置文件文件夹信息: ${file.name} -> 文件夹: ${folderName}, ID: ${folderId}`);
    uploadFileWithBinaryChunks(file);
}

// 流式写入接收处理函数 - 直接写入硬盘，不占用内存
window.handleBinaryChunkStream = async function handleBinaryChunkStream(message) {
    const { fileId, chunkIndex, totalChunks, chunkData, fileName, fileSize, folderName, folderId, relativePath } = message.data;
    
    console.log(`🌊 【流式接收】chunk ${chunkIndex}/${totalChunks}, 文件: ${fileName}${folderName ? `, 文件夹: ${folderName}` : ''}${relativePath ? `, 相对路径: ${relativePath}` : ''}`);
    
    try {
        // 检查是否已初始化流式写入器
        if (!window.streamWriters) {
            window.streamWriters = new Map();
        }
        
        let streamWriter = window.streamWriters.get(fileId);
        
        // 如果还没有写入器，创建并初始化
        if (!streamWriter) {
            streamWriter = new StreamFileWriter(fileId, fileName, totalChunks, folderName, relativePath);
            
            // 初始化流式写入器
            console.log(`🌊 【流式接收】初始化流式写入器...`);
            await streamWriter.initialize();
            
            window.streamWriters.set(fileId, streamWriter);
            
            // 添加到活跃传输列表
            if (typeof activeTransfers !== 'undefined') {
                activeTransfers.set(fileId, {
                    type: 'receive',
                    cancelled: false,
                    fileName: fileName,
                    folderName: folderName
                });
            }
            
            // 显示等待提示
            showNotification(`正在接收文件: ${fileName}`, 'info');
        }
        
        // 检查是否已取消
        const transfer = activeTransfers.get(fileId);
        if (transfer && transfer.cancelled) {
            console.log(`🌊 【流式接收】传输已取消: ${fileId}`);
            await streamWriter.cleanup();
            window.streamWriters.delete(fileId);
            activeTransfers.delete(fileId);
            return;
        }
        
        // 写入chunk到文件流
        await streamWriter.writeChunk(chunkIndex, chunkData);
        
        // 如果是文件夹传输，只显示文件夹进度（文件数），不显示每个文件的chunk进度
        if (folderName) {
            // 文件夹传输：不显示单个文件的chunk进度
            // 文件夹进度在初始化时已更新，不需要每次chunk都更新
        } else {
            // 单个文件传输：显示chunk进度
            showChunkTransferStatus(`流式接收: ${fileName}`, streamWriter.receivedChunks.size, totalChunks);
            updateChunkProgress(streamWriter.receivedChunks.size, totalChunks);
        }
        
        // 检查是否接收完成
        if (streamWriter.receivedChunks.size === totalChunks) {
            console.log(`🌊 【流式接收】所有chunks接收完成，开始完成文件: ${fileName}`);
            
            // 完成文件写入
            const result = await streamWriter.finalize();
            
            // 如果是文件夹传输，文件保存完成后才统计
            if (folderName && typeof updateFolderMessageStatus === 'function') {
                const folderTransfer = window.folderTransfers.get(folderName);
                if (folderTransfer) {
                    // 文件保存完成，将文件加入已接收列表
                    folderTransfer.receivedFiles.push(fileId);
                    const progress = Math.round((folderTransfer.receivedFiles.length / folderTransfer.totalFiles) * 100);
                    
                    // 更新文件夹进度
                    updateFolderMessageStatus(folderName, false, progress, folderTransfer.receivedFiles.length);
                    
                    // 检查是否所有文件都完成了
                    if (folderTransfer.receivedFiles.length >= folderTransfer.totalFiles) {
                        updateFolderMessageStatus(folderName, true, 100, folderTransfer.totalFiles);
                    }
                }
            } else {
                // 单个文件，添加文件消息到聊天
                addFileMessage(fileName, fileSize, result.url, true, result.savedToDisk);
            }
            
            // 清理
            await streamWriter.cleanup();
            window.streamWriters.delete(fileId);
            activeTransfers.delete(fileId);
            
            showNotification(`文件 "${fileName}" 接收完成（已保存到硬盘）`, 'success');
            
            // 隐藏进度条
            setTimeout(() => {
                const statusElement = document.getElementById('chunkTransferStatus');
                if (statusElement) {
                    statusElement.classList.remove('show');
                }
            }, 2000);
        }
        
        // 发送chunk确认
        const ackMessage = {
            type: 'chunkAck',
            data: {
                fileId: fileId,
                chunkIndex: chunkIndex,
                senderId: appState.currentUser.id,
                targetUserId: message.data.senderId
            }
        };
        sendServerMessage(ackMessage);
        
    } catch (error) {
        console.error(`🌊 【流式接收】处理失败:`, error);
        showNotification(`流式接收失败: ${error.message}`, 'error');
        
        // 清理
        if (window.streamWriters && window.streamWriters.has(fileId)) {
            const writer = window.streamWriters.get(fileId);
            await writer.cleanup();
            window.streamWriters.delete(fileId);
        }
        if (activeTransfers && activeTransfers.has(fileId)) {
            activeTransfers.delete(fileId);
        }
    }
}

// 根据接收方式选择处理函数的包装函数
window.handleBinaryChunkByMode = async function handleBinaryChunkByMode(message) {
    // 获取用户选择的接收方式
    const receiveModeRadio = document.querySelector('input[name="receiveMode"]:checked');
    const receiveMode = receiveModeRadio ? receiveModeRadio.value : 'memory';
    
    console.log(`📥 【接收方式】${receiveMode === 'memory' ? '内存传输' : '流式写入'}`);
    
    if (receiveMode === 'stream') {
        // 流式写入 - 直接写入硬盘
        return await handleBinaryChunkStream(message);
    } else {
        // 内存传输 - 使用原有的handleBinaryChunk逻辑
        // 这部分逻辑在chat.html的handleBinaryChunk函数中
        return window.originalHandleBinaryChunk ? window.originalHandleBinaryChunk(message) : null;
    }
}