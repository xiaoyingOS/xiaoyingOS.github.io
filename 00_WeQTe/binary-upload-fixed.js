// 临时文件写入器类 - 用于流式处理大文件
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

// 使用chat.html中的全局变量
window.uploadFileWithBinaryChunks = async function uploadFileWithBinaryChunks(file) {
    if (!appState.connectedUser) {
        showNotification('请先连接到用户', 'error');
        return;
    }
    
    // 检查并发上传限制
    const activeUploads = Array.from(window.activeTransfers?.values() || [])
        .filter(transfer => transfer.type === 'upload' && !transfer.cancelled);
    
    if (activeUploads.length >= 3) {
        showNotification('同时上传文件数量不能超过3个', 'error');
        return;
    }
    
    const fileId = await chunkUploader.calculateFileHash(file);
    const chunks = await chunkUploader.splitFile(file);
    
    // 为当前文件创建独立的传输状态
    const transferState = {
        fileId: fileId,
        fileName: file.name,
        fileSize: file.size,
        isCancelled: false,
        sentChunks: new Set()
    };
    
    try {
        showNotification(`开始上传文件: ${file.name}`, 'info');
        
        // 添加到活跃传输列表
        if (typeof activeTransfers !== 'undefined') {
            activeTransfers.set(fileId, {
                type: 'upload',
                cancelled: false,
                fileName: file.name
            });
        }
        
        // 显示上传进度条
        showUploadChunkTransferStatus(file.name, 0, chunks.length);
        
        // 使用单发送器模式，确保发送-接收同步
        console.log(`使用单发送器模式传输文件: ${transferState.fileName}`);
        await startFileSender(transferState, chunks);
        
    } catch (error) {
        console.error('文件上传失败:', error);
        showNotification(`文件上传失败: ${error.message}`, 'error');
    }
};

// 单发送器函数 - 确保严格的发送-接收同步
async function startFileSender(transferState, chunks) {
    console.log(`启动单发送器 for ${transferState.fileName}`);
    
    try {
        for (let chunkIndex = 0; chunkIndex < chunks.length; chunkIndex++) {
            // 检查取消状态
            if (transferState.isCancelled) {
                console.log(`文件 ${transferState.fileName} 已取消，发送器停止`);
                break;
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
                        targetUserId: appState.connectedUser.id
                    }
                };
                
                sendServerMessage(message);
                console.log(`已发送 chunk ${chunkIndex}`);
                
                // 更新进度
                updateUploadChunkProgress(chunkIndex + 1, chunks.length);
                
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
                targetUserId: appState.connectedUser.id
            }
        };
        
        sendServerMessage(completeMessage);
        showNotification(`文件 ${transferState.fileName} 上传完成`, 'success');
        
        // 清理状态
        if (typeof activeTransfers !== 'undefined') {
            activeTransfers.delete(transferState.fileId);
        }
        
        // 隐藏进度条
        setTimeout(() => {
            const statusElement = document.getElementById("uploadChunkTransferStatus");
            if (statusElement) {
                statusElement.classList.remove("show");
            }
        }, 2000);
        
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

// 文件合并函数 - 支持临时文件写入器
window.mergeBinaryChunks = async function mergeBinaryChunks(fileId) {
    console.log('开始合并二进制chunks:', fileId);
    
    const transfer = window.fileTransferMap.get(fileId);
    if (!transfer) {
        console.error('找不到传输状态:', fileId);
        return;
    }
    
    try {
        // 使用临时文件写入器进行组装
        if (transfer.fileWriter) {
            console.log('使用临时文件写入器组装文件');
            const result = await transfer.fileWriter.assembleFile();
            
            // 创建下载链接
            const downloadLink = document.createElement('a');
            downloadLink.href = result.url;
            downloadLink.download = transfer.name;
            downloadLink.textContent = `下载 ${transfer.name} (${(result.size / 1024 / 1024).toFixed(1)}MB)`;
            downloadLink.style.display = 'block';
            downloadLink.style.margin = '10px 0';
            downloadLink.style.padding = '10px';
            downloadLink.style.backgroundColor = '#4CAF50';
            downloadLink.style.color = 'white';
            downloadLink.style.textDecoration = 'none';
            downloadLink.style.borderRadius = '5px';
            
            // 添加到聊天消息
            const messageContainer = document.querySelector('.chat-messages');
            if (messageContainer) {
                const systemMessage = document.createElement('div');
                systemMessage.className = 'system-message';
                systemMessage.innerHTML = `
                    <div style="color: #666; font-style: italic; margin: 10px 0;">
                        📁 文件传输完成: ${transfer.name}
                    </div>
                `;
                systemMessage.appendChild(downloadLink);
                messageContainer.appendChild(systemMessage);
                messageContainer.scrollTop = messageContainer.scrollHeight;
            }
            
            showNotification(`文件 ${transfer.name} 接收完成`, 'success');
        } else {
            console.error('找不到文件写入器');
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

// 处理文件拖拽
window.handleFiles = function handleFiles(files) {
    for (const file of files) {
        uploadFileWithBinaryChunks(file);
    }
};