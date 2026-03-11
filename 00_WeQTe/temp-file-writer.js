// 临时文件写入器类 - 用于流式处理大文件
class TempFileWriter {
    constructor(fileId, fileName, totalChunks) {
        this.fileId = fileId;
        this.fileName = fileName;
        this.totalChunks = totalChunks;
        this.chunks = new Map(); // chunkIndex -> data
        this.receivedChunks = new Set();
        console.log(`📂 【初始化】临时文件写入器创建: ${fileName} (${fileId})`);
    }
    
    async writeChunk(chunkIndex, data) {
        console.log(`💾 【写入】准备写入chunk ${chunkIndex}, 大小: ${(data.length / 1024).toFixed(2)}KB`);
        
        try {
            // 存储到chunks Map中
            this.chunks.set(chunkIndex, data);
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
            // 检查所有chunks
            let missingChunks = [];
            for (let i = 0; i < this.totalChunks; i++) {
                if (!this.chunks.has(i)) {
                    missingChunks.push(i);
                }
            }
            
            if (missingChunks.length > 0) {
                console.error(`❌ 【错误】缺少chunks: ${missingChunks.join(', ')}`);
                return null;
            }
            
            console.log(`🔄 【合并】开始合并 ${this.chunks.size} 个chunks...`);
            
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