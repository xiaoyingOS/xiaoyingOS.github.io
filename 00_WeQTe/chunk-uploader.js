// 高效分块文件传输系统
class ChunkUploader {
    constructor() {
        // 基础分块大小
        this.baseChunkSize = 13 * 1024 * 1024; // 13MB
        
        // 根据网络状况动态调整
        this.dynamicChunkSize = this.baseChunkSize;
        this.uploadHistory = [];
        
        // 存储上传状态
        this.uploadStates = new Map(); // fileId -> upload state
        this.activeUploads = new Set(); // 当前活跃的上传
    }
    
    // 根据网络速度调整分块大小
    adjustChunkSize(speedKBps) {
        if (speedKBps > 5000) { // 高速网络 (>40Mbps)
            this.dynamicChunkSize = 20 * 1024 * 1024; // 20MB
        } else if (speedKBps > 1000) { // 中速网络 (>8Mbps)
            this.dynamicChunkSize = 13 * 1024 * 1024; // 13MB
        } else { // 低速网络
            this.dynamicChunkSize = 5 * 1024 * 1024; // 5MB
        }
        
        console.log(`调整分块大小为: ${this.dynamicChunkSize / 1024 / 1024}MB`);
    }
    
    // 计算文件ID，支持多文件且更高效
    async calculateFileId(file) {
        // 使用微秒时间戳确保唯一性
        const microTimestamp = Date.now() * 1000 + Math.floor(Math.random() * 1000);
        const random = Math.random().toString(36).substr(2, 9);
        
        // 包含相对路径信息（如果有的话）
        const filePath = file.relativePath || file.name;
        const fileInfo = `${filePath}_${file.size}_${file.lastModified}_${microTimestamp}_${random}`;
        
        // 只对文件信息进行哈希，而不是整个文件内容
        const encoder = new TextEncoder();
        const data = encoder.encode(fileInfo);
        const hashBuffer = await crypto.subtle.digest('SHA-256', data);
        const hashArray = Array.from(new Uint8Array(hashBuffer));
        const hashHex = hashArray.map(b => b.toString(16).padStart(2, '0')).join('');
        
        return hashHex;
    }
    
    // 计算分块SHA256
    async calculateChunkHash(chunk) {
        const hashBuffer = await crypto.subtle.digest('SHA-256', chunk);
        const hashArray = Array.from(new Uint8Array(hashBuffer));
        return hashArray.map(b => b.toString(16).padStart(2, '0')).join('');
    }
    
    // 分割文件为chunks
    splitFile(file, chunkSize = this.dynamicChunkSize) {
        const chunks = [];
        let offset = 0;
        
        // 处理空文件
        if (file.size === 0) {
            chunks.push({
                data: new Blob([], { type: file.type || 'application/octet-stream' }),
                index: 0,
                size: 0,
                offset: 0
            });
            return chunks;
        }
        
        while (offset < file.size) {
            const chunk = file.slice(offset, offset + chunkSize);
            chunks.push({
                data: chunk,
                index: chunks.length,
                size: chunk.size,
                offset: offset
            });
            offset += chunkSize;
        }
        
        return chunks;
    }
    
    // 创建上传状态
    createUploadState(file, fileId, chunks) {
        const uploadState = {
            fileId: fileId,
            fileName: file.name,
            fileSize: file.size,
            totalChunks: chunks.length,
            uploadedChunks: new Set(),
            chunkHashes: new Map(),
            startTime: Date.now(),
            speedHistory: [],
            isPaused: false,
            isCompleted: false
        };
        
        // 不预计算hash，改为实时计算
        this.uploadStates.set(fileId, uploadState);
        return uploadState;
    }
    
    // 上传单个chunk
    async uploadChunk(fileId, chunk, uploadState) {
        try {
            // 转换为ArrayBuffer
            const arrayBuffer = await chunk.data.arrayBuffer();
            
            // 创建二进制数据
            const uint8Array = new Uint8Array(arrayBuffer);
            
            // 异步计算hash，不阻塞发送
            const hashPromise = this.calculateChunkHash(arrayBuffer);
            
            // 立即准备消息，hash后续填充
            const chunkMessage = {
                type: 'binaryChunk',
                data: {
                    fileId: fileId,
                    chunkIndex: chunk.index,
                    totalChunks: uploadState.totalChunks,
                    chunkSize: chunk.size,
                    chunkHash: null, // 先设为null
                    fileName: uploadState.fileName,
                    fileSize: uploadState.fileSize,
                    binaryData: Array.from(uint8Array) // 转换为数组传输
                }
            };
            
            // 异步计算hash并更新
            hashPromise.then(hash => {
                chunkMessage.data.chunkHash = hash;
            }).catch(err => {
                console.error('Hash计算失败:', err);
            });
            
            return chunkMessage;
        } catch (error) {
            console.error('Chunk上传失败:', error);
            throw error;
        }
    }
    
    // 处理chunk确认
    handleChunkAck(fileId, chunkIndex, uploadSpeed) {
        const uploadState = this.uploadStates.get(fileId);
        if (!uploadState) return;
        
        uploadState.uploadedChunks.add(chunkIndex);
        uploadState.speedHistory.push(uploadSpeed);
        
        // 动态调整分块大小
        if (uploadState.speedHistory.length > 5) {
            const avgSpeed = uploadState.speedHistory.slice(-5).reduce((a, b) => a + b) / 5;
            this.adjustChunkSize(avgSpeed);
        }
        
        // 检查是否完成
        if (uploadState.uploadedChunks.size === uploadState.totalChunks) {
            uploadState.isCompleted = true;
            this.activeUploads.delete(fileId);
            console.log(`文件 ${uploadState.fileName} 上传完成`);
        }
    }
    
    // 获取上传进度
    getUploadProgress(fileId) {
        const uploadState = this.uploadStates.get(fileId);
        if (!uploadState) return null;
        
        return {
            uploadedChunks: uploadState.uploadedChunks.size,
            totalChunks: uploadState.totalChunks,
            percentage: (uploadState.uploadedChunks.size / uploadState.totalChunks) * 100,
            speed: uploadState.speedHistory.slice(-1)[0] || 0,
            isCompleted: uploadState.isCompleted,
            isPaused: uploadState.isPaused
        };
    }
    
    // 暂停上传
    pauseUpload(fileId) {
        const uploadState = this.uploadStates.get(fileId);
        if (uploadState) {
            uploadState.isPaused = true;
        }
    }
    
    // 恢复上传
    resumeUpload(fileId) {
        const uploadState = this.uploadStates.get(fileId);
        if (uploadState) {
            uploadState.isPaused = false;
        }
    }
    
    // 取消上传
    cancelUpload(fileId) {
        this.uploadStates.delete(fileId);
        this.activeUploads.delete(fileId);
    }
}

// 导出供全局使用
window.ChunkUploader = ChunkUploader;