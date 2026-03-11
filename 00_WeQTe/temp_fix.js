// 临时修复文件合并函数
function mergeFileChunksFixed(fileId) {
    console.log('开始合并文件块:', fileId);
    const transfer = fileTransferMap.get(fileId);
    if (!transfer) {
        console.warn('未找到文件传输状态:', fileId);
        return;
    }

    console.log('文件传输状态:', transfer);

    // 计算总大小
    let totalSize = 0;
    for (let i = 0; i < transfer.totalChunks; i++) {
        const chunk = transfer.chunks.get(i);
        if (chunk) {
            totalSize += chunk.length;
        }
    }

    // 创建Uint8Array来存储二进制数据
    const mergedArray = new Uint8Array(totalSize);
    let offset = 0;

    // 按顺序合并文件块
    for (let i = 0; i < transfer.totalChunks; i++) {
        const chunk = transfer.chunks.get(i);
        if (!chunk) {
            console.error(`块 ${i} 不存在`);
            continue;
        }

        try {
            // 直接使用二进制数据
            mergedArray.set(chunk, offset);
            offset += chunk.length;

            console.log(`块 ${i} 处理完成，长度:`, chunk.length, '当前总长度:', offset);

        } catch (error) {
            console.error(`处理块 ${i} 时出错:`, error);
            continue;
        }
    }

    // 创建Blob
    const blob = new Blob([mergedArray], { type: 'application/octet-stream' });
    const url = URL.createObjectURL(blob);

    console.log('文件合并完成，blob大小:', blob.size, '原始大小:', transfer.size);

    // 添加文件消息到聊天
    addFileMessage(transfer.name, transfer.size, url, true);

    // 清理传输状态
    fileTransferMap.delete(fileId);

    showNotification(`文件 "${transfer.name}" 接收完成`, 'success');
}