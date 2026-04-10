        let currentPath = '';
        
        // 加载文件列表
        async function loadFileList(path = '') {
            currentPath = path;
            const fileList = document.getElementById('fileList');
            const breadcrumb = document.getElementById('breadcrumb');
            
            fileList.innerHTML = '<div class="loading"><div class="loading-spinner"></div><p>加载中...</p></div>';
            
            try {
                // 根据路径构建正确的API URL（path已经是未编码的）
                const apiUrl = path ? `/api/browse/${encodeURIComponent(path)}` : '/api/browse';
                const response = await fetch(apiUrl);
                const data = await response.json();
                
                if (data.error) {
                    fileList.innerHTML = `<div class="error-message">${data.error}</div>`;
                    return;
                }
                
                // 更新面包屑导航
                updateBreadcrumb(path);
                
                if (data.items && data.items.length > 0) {
                    // 排序：文件夹在前，文件在后，同类型按名称排序
                    const sortedItems = data.items.sort((a, b) => {
                        if (a.is_dir !== b.is_dir) {
                            return a.is_dir ? -1 : 1; // 文件夹在前
                        }
                        return a.name.localeCompare(b.name); // 按名称排序
                    });
                    
                    fileList.innerHTML = sortedItems.map(item => `
                        <div class="file-item" onclick="${item.is_dir ? `navigateTo(\`${item.name.replace(/`/g, '\\`')}\` )` : ''}">
                            <div class="file-icon ${item.is_dir ? 'folder' : 'file'}">
                                ${item.is_dir ? '📁' : getFileIcon(item.name)}
                            </div>
                            <div class="file-info">
                                <div class="file-name">${item.name}</div>
                                <div class="file-size">${formatSize(item.size)}</div>
                            </div>
                            ${!item.is_dir ? `
                            <div class="file-actions">
                                <button class="file-actions btn-download" onclick="event.stopPropagation(); downloadFile(\`${item.name.replace(/`/g, '\\`')}\` )">
                                    ⬇️ 下载
                                </button>
                                <button class="file-actions btn-delete" onclick="event.stopPropagation(); deleteItem(\`${item.name.replace(/`/g, '\\`')}\` )">
                                    🗑️ 删除
                                </button>
                            </div>
                            ` : `
                            <div class="file-actions">
                                <button class="file-actions btn-download" onclick="event.stopPropagation(); downloadFolder(\`${item.name.replace(/`/g, '\\`')}\` )">
                                    ⬇️ 下载
                                </button>
                                <button class="file-actions btn-delete" onclick="event.stopPropagation(); deleteItem(\`${item.name.replace(/`/g, '\\`')}\` )">
                                    🗑️ 删除
                                </button>
                            </div>
                            `}
                        </div>
                    `).join('');
                } else {
                    fileList.innerHTML = '<p style="text-align: center; color: #6c757d; padding: 40px;">此文件夹为空</p>';
                }
            } catch (error) {
                fileList.innerHTML = `<div class="error-message">加载失败: ${error.message}</div>`;
            }
        }
        
        // 更新面包屑导航
        function updateBreadcrumb(path) {
            const breadcrumb = document.getElementById('breadcrumb');
            const parts = path.split('/').filter(p => p);
            
            let html = '<span class="breadcrumb-item" onclick="loadFileList(\'\')">根目录</span>';
            
            let currentPath = '';
            parts.forEach((part, index) => {
                currentPath += (currentPath ? '/' : '') + part;
                html += ` <span class="breadcrumb-item">›</span>`;
                // 不编码路径，因为loadFileList会在fetch时编码
                html += `<span class="breadcrumb-item ${index === parts.length - 1 ? 'active' : ''}" onclick="loadFileList(\`${currentPath.replace(/`/g, '\\`')}\` )">${part}</span>`;
            });
            
            breadcrumb.innerHTML = html;
        }
        
        // 导航到子文件夹
        function navigateTo(folderName) {
            const newPath = currentPath ? `${currentPath}/${folderName}` : folderName;
            loadFileList(newPath);
        }
        
        // 获取文件图标
        function getFileIcon(filename) {
            const ext = filename.split('.').pop().toLowerCase();
            const icons = {
                'pdf': '📄',
                'doc': '📝',
                'docx': '📝',
                'xls': '📊',
                'xlsx': '📊',
                'ppt': '📈',
                'pptx': '📈',
                'jpg': '🖼️',
                'jpeg': '🖼️',
                'png': '🖼️',
                'gif': '🖼️',
                'mp4': '🎬',
                'mp3': '🎵',
                'zip': '📦',
                'rar': '📦',
                'txt': '📄',
                'html': '🌐',
                'css': '🎨',
                'js': '⚡',
                'json': '📋',
            };
            return icons[ext] || '📄';
        }
        
        // 格式化文件大小
        function formatSize(bytes) {
            if (bytes === 0) return '0 B';
            const k = 1024;
            const sizes = ['B', 'KB', 'MB', 'GB', 'TB'];
            const i = Math.floor(Math.log(bytes) / Math.log(k));
            return parseFloat((bytes / Math.pow(k, i)).toFixed(2)) + ' ' + sizes[i];
        }
        
        // 上传文件（支持流式分块上传 + 动态并发控制）
        async function uploadFiles(files) {
            console.log('开始上传，文件数量:', files.length);
            
            const uploadProgress = document.getElementById('uploadProgress');
            const progressBar = document.getElementById('progressBar');
            const progressText = document.getElementById('progressText');
            
            uploadProgress.style.display = 'block';
            
            // 记录上传开始时间
            const uploadStartTime = Date.now();
            
            // 格式化时间显示
            function formatElapsedTime(ms) {
                const seconds = Math.floor(ms / 1000);
                const minutes = Math.floor(seconds / 60);
                const remainingSeconds = seconds % 60;
                return `${minutes}分${remainingSeconds}秒`;
            }
            
            let uploaded = 0;
            const total = files.length;
            const CHUNK_SIZE = 100 * 1024 * 1024; // 100MB chunk size
            
            // 预先计算每个文件的块数
            const fileChunks = files.map(file => Math.ceil(file.size / CHUNK_SIZE));
            const totalChunksAll = fileChunks.reduce((sum, chunks) => sum + chunks, 0);
            console.log(`总文件数: ${total}, 总块数: ${totalChunksAll}`);
            
            // 累计已完成文件的总块数
            let completedFilesChunks = 0;
            
            for (const file of files) {
                const totalChunks = fileChunks[uploaded]; // 当前文件的块数
                let uploadSuccess = false; // 上传成功标志
                
                console.log('处理文件:', file.name, '大小:', file.size, 'webkitRelativePath:', file.webkitRelativePath);
                
                // 获取文件名和路径
                let filename = file.name;
                let filepath = '';
                
                // 如果是文件夹上传（有 webkitRelativePath）
                if (file.webkitRelativePath) {
                    // webkitRelativePath 格式: "folder/subfolder/file.txt"
                    const pathParts = file.webkitRelativePath.split('/');
                    filename = pathParts[pathParts.length - 1]; // 文件名
                    const relativePath = pathParts.slice(0, pathParts.length - 1).join('/'); // 相对路径
                    
                    console.log('文件夹上传 - filename:', filename, 'relativePath:', relativePath, 'currentPath:', currentPath);
                    
                    // 如果有当前路径，合并当前路径和相对路径
                    if (currentPath && relativePath) {
                        filepath = currentPath + '/' + relativePath;
                    } else if (relativePath) {
                        filepath = relativePath;
                    } else if (currentPath) {
                        filepath = currentPath;
                    }
                } else {
                    // 单个文件上传
                    if (currentPath) {
                        filepath = currentPath;
                    }
                }
                
                console.log('上传到:', filepath, '文件名:', filename);
                
                // 判断是否需要分块上传
                if (file.size > CHUNK_SIZE) {
                    // 并发分块上传（利用HTTP/2多路复用）
                    console.log('使用并发分块上传，文件大小:', file.size);
                    let chunksUploaded = 0;
                    let chunksFailed = 0;
                    const MAX_RETRIES = 3;  // 重试次数
                    const MAX_CONCURRENT_UPLOADS = 5;  // 并发上传数
                    
                    // 创建所有分块任务
                    const chunkTasks = [];
                    for (let chunkIndex = 0; chunkIndex < totalChunks; chunkIndex++) {
                        const start = chunkIndex * CHUNK_SIZE;
                        const end = Math.min((chunkIndex + 1) * CHUNK_SIZE, file.size);
                        const chunkSize = end - start;
                        
                        chunkTasks.push({
                            index: chunkIndex,
                            start: start,
                            end: end,
                            size: chunkSize,
                            retries: 0,
                            status: 'pending'  // pending, uploading, success, failed
                        });
                    }
                    
                    // 并发上传函数
                    const uploadChunkWithRetry = async (task) => {
                        let chunk = null; // 在外部声明 chunk 变量以便后续释放
                        
                        while (task.retries <= MAX_RETRIES && task.status === 'pending') {
                            try {
                                task.status = 'uploading';
                                console.log(`上传块 ${task.index + 1}/${totalChunks}: ${task.start}-${task.end} (${task.size} bytes)${task.retries > 0 ? ' (重试 ' + task.retries + '/' + MAX_RETRIES + ')' : ''}`);
                                
                                // 读取分块数据
                                chunk = await file.slice(task.start, task.end).arrayBuffer();
                                
                                // 上传分块
                                const success = await uploadChunk(
                                    chunk,
                                    filename,
                                    filepath,
                                    task.index,
                                    totalChunks,
                                    task.start,
                                    task.size
                                );
                                
                                // 上传完成后立即释放内存
                                chunk = null;
                                
                                if (success) {
                                    task.status = 'success';
                                    chunksUploaded++;
                                    console.log(`块 ${task.index + 1}/${totalChunks} 上传成功`);
                                    return;
                                } else {
                                    task.retries++;
                                    task.status = 'pending';
                                }
                            } catch (error) {
                                console.error(`块 ${task.index + 1}/${totalChunks} 上传异常:`, error);
                                task.retries++;
                                task.status = 'pending';
                                // 释放内存
                                chunk = null;
                            }
                        }
                        
                        if (task.status !== 'success') {
                            task.status = 'failed';
                            chunksFailed++;
                            console.error(`块 ${task.index + 1}/${totalChunks} 上传失败`);
                        }
                    };
                    
                    // 并发执行上传
                    const uploadPromises = [];
                    for (let i = 0; i < Math.min(MAX_CONCURRENT_UPLOADS, chunkTasks.length); i++) {
                        uploadPromises.push((async () => {
                            while (true) {
                                const task = chunkTasks.find(t => t.status === 'pending');
                                if (!task) break;
                                await uploadChunkWithRetry(task);
                                
                                // 更新进度
                                const totalChunksProcessed = completedFilesChunks + chunksUploaded + chunksFailed;
                                const percentage = Math.round((totalChunksProcessed / totalChunksAll) * 100);
                                progressBar.style.width = percentage + '%';
                                const elapsedTime = formatElapsedTime(Date.now() - uploadStartTime);
                                progressText.textContent = `上传中... ${uploaded}/${total} 文件, ${chunksUploaded}/${totalChunks} 块成功, ${chunksFailed} 块失败 (${percentage}%) - 用时: ${elapsedTime}`;
                            }
                        })());
                    }
                    
                    // 等待所有上传完成
                    await Promise.all(uploadPromises);
                    
                    // 检查结果
                    if (chunksFailed > 0) {
                        uploadSuccess = false; // 有失败块，标记为失败
                        alert(`上传警告: ${file.name} - ${chunksFailed} 个分块上传失败（共${totalChunks}个分块）`);
                        console.warn(`文件 ${file.name} 上传完成，但有 ${chunksFailed} 个分块失败`);
                    } else {
                        console.log('文件分块上传完成:', file.name);
                    }
                } else {
                    // 单次上传（小文件）
                    console.log('使用单次上传，文件大小:', file.size);
                    
                    // 使用 FileReader 读取文件内容
                    let fileContent = await new Promise((resolve) => {
                        const reader = new FileReader();
                        reader.onload = (e) => {
                            console.log('文件读取完成，大小:', e.target.result.byteLength);
                            resolve(e.target.result);
                        };
                        reader.onerror = (e) => {
                            console.error('文件读取失败:', e);
                            resolve(null);
                        };
                        reader.readAsArrayBuffer(file);
                    });
                    
                    if (!fileContent) {
                        console.error('读取文件失败:', file.name);
                        continue;
                    }
                    
                    // 使用 XMLHttpRequest 发送文件数据
                    const xhr = new XMLHttpRequest();
                    xhr.open('POST', '/upload', true);
                    
                    // 设置请求头（URL编码处理中文字符）
                    if (filename) {
                        xhr.setRequestHeader('X-Filename', encodeURIComponent(filename));
                    }
                    if (filepath) {
                        xhr.setRequestHeader('X-File-Path', encodeURIComponent(filepath));
                    }
                    
                    xhr.onload = function() {
                        console.log('上传响应:', file.name, xhr.status, xhr.responseText);
                        if (xhr.status === 200) {
                            uploadSuccess = true; // 标记上传成功
                            // 使用块数计算进度
                            const totalChunksProcessed = completedFilesChunks + 1;
                            const percentage = Math.round((totalChunksProcessed / totalChunksAll) * 100);
                            progressBar.style.width = percentage + '%';
                            const elapsedTime = formatElapsedTime(Date.now() - uploadStartTime);
                            progressText.textContent = `上传中... ${uploaded}/${total} 文件 (1/1 块成功) (${percentage}%) - 用时: ${elapsedTime}`;
                        } else {
                            console.error('上传失败:', file.name, xhr.status, xhr.responseText);
                            alert(`上传失败: ${file.name} - ${xhr.status} - ${xhr.responseText}`);
                        }
                    };
                    
                    xhr.onerror = function(e) {
                        console.error('上传失败:', file.name, e);
                        alert(`上传失败: ${file.name} - 网络错误`);
                    };
                    
                    xhr.onloadend = function() {
                        // 请求完成后，强制释放内存引用
                        xhr.abort();
                    };
                    
                    console.log('发送请求:', file.name, '内容大小:', fileContent.byteLength);
                    xhr.send(fileContent);
                    
                    // 等待请求完成
                    await new Promise((resolve) => {
                        xhr.onloadend = resolve;
                        xhr.onerror = resolve;
                    });
                    
                    // 上传完成后立即释放内存
                    fileContent = null;
                    
                    // 只有上传成功时才累加计数器
                    if (uploadSuccess) {
                        completedFilesChunks += totalChunks;
                        uploaded++;
                    }
                }
            }
            
            const totalElapsedTime = formatElapsedTime(Date.now() - uploadStartTime);
            alert(`上传完成！共上传 ${uploaded} 个文件，总用时: ${totalElapsedTime}`);
            
            uploadProgress.style.display = 'none';
            progressBar.style.width = '0%';
            closeUploadModal();
            refreshFileList();
        }
        
        // 上传单个文件块
        function uploadChunk(chunk, filename, filepath, chunkIndex, totalChunks, chunkOffset, chunkSize) {
            return new Promise((resolve) => {
                const xhr = new XMLHttpRequest();
                xhr.open('POST', '/upload', true);
                
                // 设置请求头
                xhr.setRequestHeader('X-Filename', encodeURIComponent(filename));
                xhr.setRequestHeader('X-File-Path', encodeURIComponent(filepath));
                xhr.setRequestHeader('X-Chunk-Index', chunkIndex.toString());
                xhr.setRequestHeader('X-Total-Chunks', totalChunks.toString());
                xhr.setRequestHeader('X-Chunk-Offset', chunkOffset.toString());
                xhr.setRequestHeader('X-Chunk-Size', chunkSize.toString());
                xhr.setRequestHeader('Content-Type', 'application/octet-stream');
                
                xhr.onload = function() {
                    if (xhr.status === 200) {
                        console.log(`块 ${chunkIndex + 1}/${totalChunks} 上传成功`);
                        resolve(true);
                    } else {
                        console.error(`块 ${chunkIndex + 1}/${totalChunks} 上传失败:`, xhr.status, xhr.responseText);
                        resolve(false);
                    }
                };
                
                xhr.onerror = function(e) {
                    console.error(`块 ${chunkIndex + 1}/${totalChunks} 上传失败:`, e);
                    resolve(false);
                };
                
                xhr.onloadend = function() {
                    // 请求完成后，强制释放内存引用
                    xhr.abort();
                };
                
                xhr.send(chunk);
            });
        }
        
        // 下载文件
        function downloadFile(filename) {
            const path = currentPath ? `${currentPath}/${filename}` : filename;
            window.location.href = `/files/${encodeURIComponent(path)}`;
        }
        
        // 下载文件夹中的所有文件
        async function downloadFolder(folderName) {
            const path = currentPath ? `${currentPath}/${folderName}` : folderName;
            
            // 检测浏览器是否支持 File System API
            if ('showDirectoryPicker' in window) {
                // 使用 File System API 下载（推荐方式）
                await downloadFolderWithFileSystemAPI(folderName, path);
            } else {
                // 回退方案：使用服务器端打包
                await downloadFolderAsArchive(folderName, path);
            }
        }
        
        // 使用 File System API 下载文件夹（支持创建文件夹结构）
        async function downloadFolderWithFileSystemAPI(folderName, path) {
            try {
                // 获取文件夹内容
                const response = await fetch(`/api/browse/${encodeURIComponent(path)}`);
                const data = await response.json();
                
                if (data.error) {
                    alert(`获取文件夹内容失败: ${data.error}`);
                    return;
                }
                
                // 收集所有文件和文件夹信息（BFS 方式）
                const allItems = [];
                const queue = [{ path: path, relativePath: '' }];
                
                while (queue.length > 0) {
                    const current = queue.shift();
                    
                    try {
                        const res = await fetch(`/api/browse/${encodeURIComponent(current.path)}`);
                        const result = await res.json();
                        
                        if (result.items) {
                            for (const item of result.items) {
                                const itemRelativePath = current.relativePath ? `${current.relativePath}/${item.name}` : item.name;
                                const itemFullPath = current.path ? `${current.path}/${item.name}` : item.name;
                                
                                allItems.push({
                                    name: item.name,
                                    path: itemFullPath,
                                    relativePath: itemRelativePath,
                                    is_dir: item.is_dir,
                                    size: item.size
                                });
                                
                                if (item.is_dir) {
                                    queue.push({
                                        path: itemFullPath,
                                        relativePath: itemRelativePath
                                    });
                                }
                            }
                        }
                    } catch (error) {
                        console.error(`读取文件夹失败 ${current.path}:`, error);
                    }
                }
                
                if (allItems.length === 0) {
                    alert('此文件夹中没有文件');
                    return;
                }
                
                if (!confirm(`确定要下载文件夹 "${folderName}" 吗？\n\n包含 ${allItems.length} 个项目（文件和文件夹）\n将使用 File System API 创建文件夹结构。`)) {
                    return;
                }
                
                // 让用户选择下载位置
                const dirHandle = await window.showDirectoryPicker({
                    mode: 'readwrite',
                    startIn: 'downloads'
                });
                
                // 创建目标文件夹（以源文件夹名命名）
                const targetFolderHandle = await dirHandle.getDirectoryHandle(folderName, { create: true });
                
                // 显示下载进度
                const progressDiv = document.createElement('div');
                progressDiv.style.cssText = 'position: fixed; top: 50%; left: 50%; transform: translate(-50%, -50%); background: rgba(0,0,0,0.9); color: white; padding: 20px; border-radius: 10px; z-index: 10000; text-align: center; min-width: 300px;';
                progressDiv.innerHTML = `
                    <div style="font-size: 18px; margin-bottom: 10px;">正在下载文件夹 "${folderName}"</div>
                    <div style="font-size: 14px; color: #aaa; margin-bottom: 10px;">共 ${allItems.length} 个项目</div>
                    <div style="margin-bottom: 10px;">
                        <div style="background: #333; border-radius: 5px; overflow: hidden;">
                            <div id="progressBar" style="height: 20px; background: linear-gradient(90deg, #667eea, #764ba2); width: 0%; transition: width 0.3s;"></div>
                        </div>
                    </div>
                    <div id="progressText" style="font-size: 14px;">0 / ${allItems.length}</div>
                `;
                document.body.appendChild(progressDiv);
                
                // 先创建所有文件夹结构
                for (const item of allItems) {
                    if (item.is_dir) {
                        const pathParts = item.relativePath.split('/');
                        let currentHandle = targetFolderHandle;
                        
                        for (let i = 0; i < pathParts.length; i++) {
                            currentHandle = await currentHandle.getDirectoryHandle(pathParts[i], { create: true });
                        }
                    }
                }
                
                // 下载所有文件
                let downloaded = 0;
                const fileCount = allItems.filter(i => !i.is_dir).length;
                
                for (const item of allItems) {
                    if (!item.is_dir) {
                        try {
                            // 下载文件
                            const fileResponse = await fetch(`/files/${encodeURIComponent(item.path)}`);
                            const fileBlob = await fileResponse.blob();
                            
                            // 获取目标文件夹句柄
                            const pathParts = item.relativePath.split('/');
                            let currentHandle = targetFolderHandle;
                            
                            for (let i = 0; i < pathParts.length - 1; i++) {
                                currentHandle = await currentHandle.getDirectoryHandle(pathParts[i]);
                            }
                            
                            // 创建文件
                            const fileHandle = await currentHandle.getFileHandle(item.name, { create: true });
                            const writable = await fileHandle.createWritable();
                            await writable.write(fileBlob);
                            await writable.close();
                            
                            downloaded++;
                            
                            // 更新进度
                            const progressBar = document.getElementById('progressBar');
                            const progressText = document.getElementById('progressText');
                            const percentage = (downloaded / fileCount * 100).toFixed(1);
                            
                            if (progressBar) progressBar.style.width = `${percentage}%`;
                            if (progressText) progressText.textContent = `${downloaded} / ${fileCount} 个文件 (${percentage}%)`;
                            
                        } catch (error) {
                            console.error(`下载文件失败 ${item.name}:`, error);
                        }
                    }
                }
                
                // 完成下载
                document.body.removeChild(progressDiv);
                alert(`下载完成！已将文件夹 "${folderName}" 及其所有内容下载到您选择的位置。`);
                
            } catch (error) {
                if (error.name === 'AbortError') {
                    console.log('用户取消了选择文件夹');
                } else {
                    console.error('下载文件夹失败:', error);
                    alert('下载文件夹失败，请重试');
                }
            }
        }
        
        // 回退方案：下载为压缩包
        async function downloadFolderAsArchive(folderName, path) {
            try {
                // 获取文件夹内容
                const response = await fetch(`/api/browse/${encodeURIComponent(path)}`);
                const data = await response.json();
                
                if (data.error) {
                    alert(`获取文件夹内容失败: ${data.error}`);
                    return;
                }
                
                // 递归获取所有文件
                const allFiles = [];
                
                async function collectFiles(folderPath, prefix = '') {
                    try {
                        const res = await fetch(`/api/browse/${encodeURIComponent(folderPath)}`);
                        const result = await res.json();
                        
                        if (result.items) {
                            for (const item of result.items) {
                                const itemPath = folderPath ? `${folderPath}/${item.name}` : item.name;
                                
                                if (item.is_dir) {
                                    // 递归处理子文件夹
                                    await collectFiles(itemPath, `${prefix}${item.name}/`);
                                } else {
                                    // 收集文件
                                    allFiles.push({
                                        name: item.name,
                                        path: itemPath,
                                        size: item.size
                                    });
                                }
                            }
                        }
                    } catch (error) {
                        console.error(`收集文件失败 ${folderPath}:`, error);
                    }
                }
                
                await collectFiles(path);
                
                if (allFiles.length === 0) {
                    alert('此文件夹中没有文件');
                    return;
                }
                
                if (!confirm(`您的浏览器不支持 File System API。\n\n确定要下载文件夹 "${folderName}" 中的 ${allFiles.length} 个文件吗？\n\n文件将下载为 TAR.XZ 压缩包。`)) {
                    return;
                }
                
                // 显示下载进度
                const progressDiv = document.createElement('div');
                progressDiv.style.cssText = 'position: fixed; top: 50%; left: 50%; transform: translate(-50%, -50%); background: rgba(0,0,0,0.9); color: white; padding: 20px; border-radius: 10px; z-index: 10000; text-align: center; min-width: 300px;';
                progressDiv.innerHTML = `
                    <div style="font-size: 18px; margin-bottom: 10px;">正在打包文件夹 "${folderName}"</div>
                    <div style="font-size: 14px; color: #aaa; margin-bottom: 10px;">共 ${allFiles.length} 个文件</div>
                    <div style="margin-bottom: 10px;">
                        <div style="background: #333; border-radius: 5px; overflow: hidden;">
                            <div id="progressBar" style="height: 20px; background: linear-gradient(90deg, #667eea, #764ba2); width: 0%; transition: width 0.3s;"></div>
                        </div>
                    </div>
                    <div id="progressText" style="font-size: 14px;">服务器正在打包，请稍候...</div>
                    <div style="font-size: 12px; color: #888; margin-top: 10px;">大文件可能需要较长时间，请耐心等待</div>
                `;
                document.body.appendChild(progressDiv);
                
                const downloadUrl = `/api/folder/download/${encodeURIComponent(path)}`;
                
                // 使用 fetch 下载并检测打包完成
                async function downloadWithRetry() {
                    let retryCount = 0;
                    const maxRetries = 20; // 最多重试20次
                    
                    while (retryCount < maxRetries) {
                        const response = await fetch(downloadUrl, {
                            signal: AbortSignal.timeout(10000) // 10秒超时
                        }).catch(err => {
                            if (err.name === 'TimeoutError') {
                                throw new Error('请求超时');
                            }
                            throw err;
                        });
                        
                        if (response.status === 202) {
                            // 正在打包，等待2秒后重试
                            retryCount++;
                            const progressText = document.getElementById('progressText');
                            if (progressText) progressText.textContent = `正在打包... (${retryCount}/${maxRetries})`;
                            await new Promise(resolve => setTimeout(resolve, 2000));
                            continue;
                        }
                        
                        if (!response.ok) {
                            throw new Error(`下载失败: ${response.status}`);
                        }
                        
                        // 打包完成，更新提示
                        const progressText = document.getElementById('progressText');
                        const progressBar = document.getElementById('progressBar');
                        if (progressText) progressText.textContent = '打包完成，正在下载...';
                        if (progressBar) progressBar.style.width = '100%';
                        
                        // 3秒后移除进度提示
                        setTimeout(() => {
                            if (progressDiv && progressDiv.parentNode) {
                                document.body.removeChild(progressDiv);
                            }
                        }, 3000);
                        
                        // 读取数据并下载
                        const reader = response.body.getReader();
                        const chunks = [];
                        
                        while (true) {
                            const { done, value } = await reader.read();
                            if (done) break;
                            chunks.push(value);
                        }
                        
                        // 创建 Blob 并下载
                        const blob = new Blob(chunks, { type: 'application/zip' });
                        const link = document.createElement('a');
                        link.href = URL.createObjectURL(blob);
                        link.download = `${folderName}.tar.zst`;
                        link.style.display = 'none';
                        document.body.appendChild(link);
                        link.click();
                        
                        setTimeout(() => {
                            document.body.removeChild(link);
                            URL.revokeObjectURL(link.href);
                        }, 100);
                        
                        return;
                    }
                    
                    throw new Error('打包超时，请稍后再试');
                }
                
                downloadWithRetry().catch(err => {
                    console.error('下载失败:', err);
                    if (progressDiv && progressDiv.parentNode) {
                        document.body.removeChild(progressDiv);
                    }
                    alert(`下载失败: ${err.message}`);
                });
                
            } catch (error) {
                console.error('下载文件夹失败:', error);
                alert('下载文件夹失败，请重试');
            }
        }
        
        // 删除项目
        async function deleteItem(name) {
            if (!confirm(`确定要删除 "${name}" 吗？`)) {
                return;
            }
            
            const path = currentPath ? `${currentPath}/${name}` : name;
            
            try {
                const response = await fetch(`/api/files/${encodeURIComponent(path)}`, {
                    method: 'DELETE'
                });
                
                const result = await response.json();
                
                if (response.ok) {
                    refreshFileList();
                } else {
                    alert(`删除失败: ${result.error}`);
                }
            } catch (error) {
                alert(`删除失败: ${error.message}`);
            }
        }
        
        // 创建文件夹
        async function createFolder() {
            const folderName = document.getElementById('folderNameInput').value.trim();
            
            if (!folderName) {
                alert('请输入文件夹名称');
                return;
            }
            
            try {
                            const response = await fetch('/api/folder/create', {
                                method: 'POST',
                                headers: {
                                    'X-Foldername': encodeURIComponent(folderName),
                                    'X-Parent-Path': currentPath ? encodeURIComponent(currentPath) : 'ROOT'
                                }
                            });                const result = await response.json();
                
                if (response.ok) {
                    closeCreateFolderModal();
                    refreshFileList();
                } else {
                    alert(`创建失败: ${result.error}`);
                }
            } catch (error) {
                alert(`创建失败: ${error.message}`);
            }
        }
        
        // 刷新文件列表
        function refreshFileList() {
            loadFileList(currentPath);
        }
        
        // 显示上传模态框
        function showUploadModal() {
            document.getElementById('uploadModal').style.display = 'flex';
        }
        
        // 关闭上传模态框
        function closeUploadModal() {
            document.getElementById('uploadModal').style.display = 'none';
            document.getElementById('fileInput').value = '';
            document.getElementById('folderInput').value = '';
        }
        
        // 显示创建文件夹模态框
        function showCreateFolderModal() {
            document.getElementById('createFolderModal').style.display = 'flex';
            document.getElementById('folderNameInput').focus();
        }
        
        // 关闭创建文件夹模态框
        function closeCreateFolderModal() {
            document.getElementById('createFolderModal').style.display = 'none';
            document.getElementById('folderNameInput').value = '';
        }
        
        // 文件输入处理
        document.getElementById('fileInput').addEventListener('change', function(e) {
            if (e.target.files.length > 0) {
                uploadFiles(e.target.files);
            }
        });
        
        // 文件夹输入处理
        document.getElementById('folderInput').addEventListener('change', function(e) {
            if (e.target.files.length > 0) {
                uploadFiles(e.target.files);
            }
        });
        
        // 拖拽上传处理
        const uploadArea = document.getElementById('uploadArea');
        
        uploadArea.addEventListener('dragover', function(e) {
            e.preventDefault();
            uploadArea.classList.add('dragover');
        });
        
        uploadArea.addEventListener('dragleave', function(e) {
            e.preventDefault();
            uploadArea.classList.remove('dragover');
        });
        
        // 使用BFS方式读取文件夹
        async function readDirectoryEntries(directory, path = '') {
            const entries = [];
            const queue = [{ entry: directory, path: path }];
            
            while (queue.length > 0) {
                const { entry: currentDir, path: currentPath } = queue.shift();
                const reader = currentDir.createReader();
                
                // 读取所有条目（可能需要多次读取）
                let allEntries = [];
                let readBatch;
                do {
                    readBatch = await new Promise((resolve, reject) => {
                        reader.readEntries(resolve, reject);
                    });
                    allEntries = allEntries.concat(readBatch);
                } while (readBatch.length > 0);
                
                for (const entry of allEntries) {
                    const entryPath = currentPath ? `${currentPath}/${entry.name}` : entry.name;
                    
                    if (entry.isFile) {
                        const file = await new Promise((resolve, reject) => {
                            entry.file(resolve, reject);
                        });
                        // 为文件设置 webkitRelativePath 属性
                        Object.defineProperty(file, 'webkitRelativePath', {
                            value: entryPath,
                            writable: false
                        });
                        entries.push(file);
                    } else if (entry.isDirectory) {
                        // 将子文件夹加入队列
                        queue.push({ entry: entry, path: entryPath });
                    }
                }
            }
            
            return entries;
        }
        
        uploadArea.addEventListener('drop', async function(e) {
            e.preventDefault();
            uploadArea.classList.remove('dragover');
            
            const items = e.dataTransfer.items;
            if (!items || items.length === 0) {
                return;
            }
            
            // 尝试从 dataTransfer.items 获取文件/文件夹
            const files = [];
            
            for (let i = 0; i < items.length; i++) {
                const item = items[i].webkitGetAsEntry();
                if (!item) continue;
                
                if (item.isFile) {
                    // 单个文件
                    const file = await new Promise((resolve, reject) => {
                        item.file(resolve, reject);
                    });
                    files.push(file);
                } else if (item.isDirectory) {
                    // 文件夹：BFS读取所有文件
                    console.log('检测到文件夹，开始BFS读取...');
                    const folderFiles = await readDirectoryEntries(item, item.name);
                    files.push(...folderFiles);
                    console.log(`文件夹读取完成，共 ${folderFiles.length} 个文件`);
                }
            }
            
            if (files.length > 0) {
                console.log(`开始上传 ${files.length} 个文件`);
                uploadFiles(files);
            }
        });
        
        // 点击模态框外部关闭
        document.getElementById('uploadModal').addEventListener('click', function(e) {
            if (e.target === this) {
                closeUploadModal();
            }
        });
        
        document.getElementById('createFolderModal').addEventListener('click', function(e) {
            if (e.target === this) {
                closeCreateFolderModal();
            }
        });
        
        // 页面加载时初始化
        loadFileList();
    </script>
</body>
</html>