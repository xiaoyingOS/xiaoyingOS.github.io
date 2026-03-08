const express = require('express');
const https = require('https');
const fs = require('fs');
const path = require('path');
const ffmpeg = require('fluent-ffmpeg');
const multer = require('multer');
const { exec } = require('child_process');

const PORT = 8691;
const app = express();

// 检测ffmpeg是否安装
function checkFFmpeg() {
    return new Promise((resolve, reject) => {
        exec('ffmpeg -version', (error, stdout, stderr) => {
            if (error) {
                console.error('❌ 错误: ffmpeg未安装！');
                console.error('');
                console.error('请先安装ffmpeg:');
                console.error('  - Termux: pkg install ffmpeg');
                console.error('  - Ubuntu/Debian: sudo apt-get install ffmpeg');
                console.error('  - CentOS/RHEL: sudo yum install ffmpeg');
                console.error('  - macOS: brew install ffmpeg');
                console.error('');
                reject(new Error('ffmpeg未安装'));
            } else {
                const version = stdout.split('\n')[0];
                console.log(`✓ ffmpeg已安装: ${version}`);
                resolve();
            }
        });
    });
}

// 创建 output 文件夹（使用绝对路径）
const scriptDir = path.resolve(__dirname);
const outputDir = path.join(scriptDir, 'output');
console.log(`📁 当前目录: ${scriptDir}`);
console.log(`📁 输出目录: ${outputDir}`);
if (!fs.existsSync(outputDir)) {
    fs.mkdirSync(outputDir, { recursive: true });
}

// 配置文件上传 - 使用磁盘存储
const upload = multer({
    storage: multer.diskStorage({
        destination: function (req, file, cb) {
            const tempDir = path.join(__dirname, 'temp');
            if (!fs.existsSync(tempDir)) {
                fs.mkdirSync(tempDir, { recursive: true });
            }
            cb(null, tempDir);
        },
        filename: function (req, file, cb) {
            cb(null, 'input_' + Date.now() + path.extname(file.originalname));
        }
    }),
    fileFilter: function (req, file, cb) {
        // 修复Windows上的中文文件名乱码问题
        // 尝试将文件名从Latin1转换为UTF-8
        try {
            const originalname = file.originalname;
            const decodedName = Buffer.from(originalname, 'latin1').toString('utf8');
            file.originalname = decodedName;
            console.log(`📄 文件名解码: ${originalname} -> ${decodedName}`);
        } catch (e) {
            console.log(`⚠️ 文件名解码失败，使用原始名称: ${e.message}`);
        }
        cb(null, true);
    },
    limits: {} // 无限制
});

// CORS中间件
app.use((req, res, next) => {
    res.header('Access-Control-Allow-Origin', '*');
    res.header('Access-Control-Allow-Methods', 'GET, POST, OPTIONS');
    res.header('Access-Control-Allow-Headers', 'Content-Type');
    res.header('Cache-Control', 'no-cache, no-store, must-revalidate');
    next();
});

app.options('*', (req, res) => {
    res.sendStatus(200);
});

// 静态文件服务
app.use(express.static(__dirname));

// 分割转换API
app.post('/api/convert-all', upload.single('video'), async (req, res) => {
    try {
        if (!req.file) {
            return res.json({ success: false, message: '没有上传文件' });
        }

        // 获取 FFmpeg 配置参数
        const ffmpegConfig = req.body.ffmpegConfig ? JSON.parse(req.body.ffmpegConfig) : {
            videoCodec: 'copy',
            crf: 23,
            preset: 'medium',
            bitrate: 'N/A',
            frameRate: 'N/A',
            resolution: 'copy',
            aspect: 'copy',
            sampleRate: '48000',
            channels: '2',
            aacBitrate: '320k',
            mp3Bitrate: '320k',
            vorbisBitrate: '320k',
            pcmBitDepth: 'pcm_s24le',
            flacBitDepth: 'flac_s24'
        };

        console.log(`📤 上传完成: ${req.file.filename} (${(req.file.size / 1024 / 1024).toFixed(2)}MB)`);
        console.log(`📄 原始文件名: ${req.file.originalname}`);
        console.log(`⚙️  FFmpeg 配置: 视频编码=${ffmpegConfig.videoCodec}, 帧率=${ffmpegConfig.frameRate}, 分辨率=${ffmpegConfig.resolution}`);
        console.log(`⚙️  音频配置: PCM位深度=${ffmpegConfig.pcmBitDepth}, FLAC位深度=${ffmpegConfig.flacBitDepth}`);
        
        const inputFile = req.file.path;
        const timestamp = Date.now();
        console.log(`📂 outputDir: ${outputDir}`);
        console.log(`📂 __dirname: ${__dirname}`);

        // 获取原始文件名（不包含扩展名）
        // 文件名已在multer的fileFilter中转换为UTF-8
        const originalFileName = path.parse(req.file.originalname).name;
        
        // 获取上传视频的容器格式
        const originalExt = path.parse(req.file.originalname).ext.toLowerCase();
        console.log(`📁 上传视频格式: ${originalExt}`);

        // 定义5种音频编码
        const audioConfigs = [
            { codec: 'aac', name: 'AAC音频' },
            { codec: 'libmp3lame', name: 'MP3音频' },
            { codec: 'libvorbis', name: 'Vorbis音频' },
            { codec: ffmpegConfig.pcmBitDepth || 'pcm_s24le', name: 'PCM无损' },
            { codec: 'flac', name: 'FLAC无损' }
        ];

        const generatedFiles = [];

        for (let i = 0; i < audioConfigs.length; i++) {
            const config = audioConfigs[i];
            
            // 根据音频格式选择合适的容器
            let container = 'mp4'; // 默认容器
            let finalContainer = container;
            let originalContainer = null; // 记录原始上传格式对应的容器
            
            // 所有格式(AAC, MP3, Vorbis, FLAC, PCM)都使用与上传视频相同的容器
            if (originalExt === '.mov') {
                container = 'mov';
            } else if (originalExt === '.mkv') {
                container = 'mkv';
            } else if (originalExt === '.mp4') {
                container = 'mp4';
            }
            
            // 记录原始容器用于回退逻辑
            originalContainer = container;
            
            // 构建输出文件名：split_${codec}_${timestamp}_${原始文件名}.${container}
            let outputFile = path.join(outputDir, `split_${config.codec}_${timestamp}_${originalFileName}.${container}`);

            console.log(`🔄 正在转换 (${i + 1}/${audioConfigs.length}): ${config.name}...`);

            try {
                await new Promise((resolve, reject) => {
                    let ffmpegCommand = ffmpeg(inputFile);
                    
                    // 构建输出选项
                    let outputOptions = [];
                    
                    // 视频编码器
                    outputOptions.push('-c:v', ffmpegConfig.videoCodec);
                    
                    // 如果不是copy模式，添加质量控制参数
                    if (ffmpegConfig.videoCodec !== 'copy') {
                        // CRF质量控制
                        if (ffmpegConfig.crf !== undefined && ffmpegConfig.crf !== 'N/A') {
                            outputOptions.push('-crf', ffmpegConfig.crf.toString());
                        }
                        // 编码预设
                        if (ffmpegConfig.preset && ffmpegConfig.preset !== 'N/A') {
                            outputOptions.push('-preset', ffmpegConfig.preset);
                        }
                    }
                    
                    // 视频比特率
                    if (ffmpegConfig.bitrate && ffmpegConfig.bitrate !== 'N/A') {
                        outputOptions.push('-b:v', ffmpegConfig.bitrate);
                    }
                    
                    // 帧率
                    if (ffmpegConfig.frameRate && ffmpegConfig.frameRate !== 'N/A') {
                        outputOptions.push('-r', ffmpegConfig.frameRate);
                    }
                    
                    // 分辨率
                    if (ffmpegConfig.resolution && ffmpegConfig.resolution !== 'copy') {
                        outputOptions.push('-vf', `scale=${ffmpegConfig.resolution}`);
                    }
                    
                    // 宽高比
                    if (ffmpegConfig.aspect && ffmpegConfig.aspect !== 'copy') {
                        outputOptions.push('-aspect', ffmpegConfig.aspect);
                    }
                    
                    // 音频采样率
                    if (ffmpegConfig.sampleRate && ffmpegConfig.sampleRate !== 'N/A') {
                        outputOptions.push('-ar', ffmpegConfig.sampleRate);
                    }
                    
                    // 音频声道
                    if (ffmpegConfig.channels && ffmpegConfig.channels !== 'N/A') {
                        outputOptions.push('-ac', ffmpegConfig.channels);
                    }
                    
                    // 根据不同的编码格式使用不同的音频参数
                    if (config.codec === 'aac') {
                        outputOptions.push('-c:a', 'aac');
                        if (ffmpegConfig.aacBitrate && ffmpegConfig.aacBitrate !== 'N/A') {
                            outputOptions.push('-b:a', ffmpegConfig.aacBitrate);
                        } else {
                            outputOptions.push('-b:a', '192k');
                        }
                    } else if (config.codec === 'libmp3lame') {
                        outputOptions.push('-c:a', 'libmp3lame');
                        if (ffmpegConfig.mp3Bitrate && ffmpegConfig.mp3Bitrate !== 'N/A') {
                            outputOptions.push('-b:a', ffmpegConfig.mp3Bitrate);
                        } else {
                            outputOptions.push('-b:a', '192k');
                        }
                    } else if (config.codec === 'libvorbis') {
                        outputOptions.push('-c:a', 'libvorbis');
                        if (ffmpegConfig.vorbisBitrate && ffmpegConfig.vorbisBitrate !== 'N/A') {
                            outputOptions.push('-b:a', ffmpegConfig.vorbisBitrate);
                        } else {
                            outputOptions.push('-b:a', '192k');
                        }
                    } else if (config.codec.startsWith('pcm_')) {
                        // PCM编码,直接使用对应的PCM编码器(编码器本身包含位深度信息)
                        outputOptions.push('-c:a', config.codec);
                    } else if (config.codec === 'flac') {
                        outputOptions.push('-c:a', 'flac');
                        // 根据配置的FLAC位深度设置采样格式
                        // 注意: FLAC使用s32表示24-bit,s32表示32-bit
                        let sampleFmt = 's32'; // 默认24-bit
                        if (ffmpegConfig.flacBitDepth) {
                            if (ffmpegConfig.flacBitDepth === 'flac_s16') {
                                sampleFmt = 's16';
                                outputOptions.push('-sample_fmt', 's16');
                            } else if (ffmpegConfig.flacBitDepth === 'flac_s24') {
                                sampleFmt = 's32'; // 24-bit FLAC使用s32
                                outputOptions.push('-sample_fmt', 's32');
                            } else if (ffmpegConfig.flacBitDepth === 'flac_s32') {
                                sampleFmt = 's32'; // 32-bit FLAC也使用s32
                                outputOptions.push('-sample_fmt', 's32');
                            }
                        }
                        console.log(`   FLAC编码参数: -c:a flac -sample_fmt ${sampleFmt}`);
                    }
                    
                    ffmpegCommand.outputOptions(outputOptions);
                    
                    // 打印FFmpeg命令
                    const commandArgs = ffmpegCommand._arguments || ffmpegCommand.options;
                    if (commandArgs) {
                        const inputOption = inputFile ? `-i "${inputFile}"` : '';
                        const outputOptionStr = outputOptions.join(' ');
                        console.log(`📝 FFmpeg命令: ffmpeg ${inputOption} ${outputOptionStr} "${outputFile}"`);
                    }
                    
                    ffmpegCommand.output(outputFile)
                        .on('progress', (progress) => {
                            if (progress.percent) {
                                console.log(`   进度: ${progress.percent.toFixed(1)}%`);
                            }
                        })
                        .on('end', () => {
                            console.log(`✅ ${config.name} 转换完成`);
                            resolve();
                        })
                        .on('error', (err) => {
                            // 先删除当前转换失败产生的空文件
                            fs.unlink(outputFile, (unlinkErr) => {
                                if (unlinkErr) {
                                    console.error(`删除失败文件失败:`, unlinkErr.message);
                                }
                            });
                            
                            // 输出原始容器失败的错误
                            console.error(`❌ ${config.name} (${container}) 转换失败:`, err.message);
                            
                            // 定义回退顺序: 根据原始格式决定
                            let fallbackSequence = [];
                            if (originalContainer === 'mov') {
                                fallbackSequence = ['mp4', 'mkv'];
                            } else if (originalContainer === 'mkv') {
                                fallbackSequence = ['mp4', 'mov'];
                            } else if (originalContainer === 'mp4') {
                                fallbackSequence = ['mov', 'mkv'];
                            } else {
                                fallbackSequence = ['mov', 'mkv'];
                            }
                            
                            // 查找当前容器在回退序列中的位置
                            let currentAttemptIndex = -1;
                            if (container === originalContainer) {
                                currentAttemptIndex = -1; // 原始格式
                            } else {
                                currentAttemptIndex = fallbackSequence.indexOf(container);
                            }
                            
                            // 计算下一个应该尝试的容器
                            if (currentAttemptIndex < fallbackSequence.length - 1) {
                                const nextContainer = fallbackSequence[currentAttemptIndex + 1];
                                console.log(`⚠️ ${config.name} ${container}容器转换失败，尝试使用${nextContainer}容器...`);
                                outputFile = path.join(outputDir, `split_${config.codec}_${timestamp}_${originalFileName}.${nextContainer}`);
                                container = nextContainer;
                                finalContainer = nextContainer;
                                
                                // 构建回退选项(根据不同格式使用不同的编码器)
                                
                                                                let fallbackOptions = [];
                                
                                                                
                                
                                                                // 视频编码器
                                
                                                                fallbackOptions.push('-c:v', ffmpegConfig.videoCodec);
                                
                                                                
                                
                                                                // 如果不是copy模式，添加质量控制参数
                                
                                                                
                                
                                                                                                if (ffmpegConfig.videoCodec !== 'copy') {
                                
                                                                
                                
                                                                                                    // CRF质量控制
                                
                                                                
                                
                                                                                                    if (ffmpegConfig.crf !== undefined && ffmpegConfig.crf !== 'N/A') {
                                
                                                                
                                
                                                                                                        fallbackOptions.push('-crf', ffmpegConfig.crf.toString());
                                
                                                                
                                
                                                                                                    }
                                
                                                                
                                
                                                                                                    // 编码预设
                                
                                                                
                                
                                                                                                    if (ffmpegConfig.preset && ffmpegConfig.preset !== 'N/A') {
                                
                                                                
                                
                                                                                                        fallbackOptions.push('-preset', ffmpegConfig.preset);
                                
                                                                
                                
                                                                                                    }
                                
                                                                
                                
                                                                                                }
                                
                                                                
                                
                                                                // 视频比特率
                                
                                                                if (ffmpegConfig.bitrate && ffmpegConfig.bitrate !== 'N/A') {
                                
                                                                    fallbackOptions.push('-b:v', ffmpegConfig.bitrate);
                                
                                                                }
                                
                                                                
                                
                                                                // 帧率
                                
                                                                if (ffmpegConfig.frameRate && ffmpegConfig.frameRate !== 'N/A') {
                                
                                                                    fallbackOptions.push('-r', ffmpegConfig.frameRate);
                                
                                                                }
                                
                                                                
                                
                                                                // 分辨率
                                
                                                                if (ffmpegConfig.resolution && ffmpegConfig.resolution !== 'copy') {
                                
                                                                    fallbackOptions.push('-vf', `scale=${ffmpegConfig.resolution}`);
                                
                                                                }
                                
                                                                
                                
                                                                // 宽高比
                                
                                                                if (ffmpegConfig.aspect && ffmpegConfig.aspect !== 'copy') {
                                
                                                                    fallbackOptions.push('-aspect', ffmpegConfig.aspect);
                                
                                                                }
                                
                                                                
                                
                                                                // 音频采样率
                                
                                                                if (ffmpegConfig.sampleRate && ffmpegConfig.sampleRate !== 'N/A') {
                                
                                                                    fallbackOptions.push('-ar', ffmpegConfig.sampleRate);
                                
                                                                }
                                
                                                                
                                
                                                                // 音频声道
                                
                                                                if (ffmpegConfig.channels && ffmpegConfig.channels !== 'N/A') {
                                
                                                                    fallbackOptions.push('-ac', ffmpegConfig.channels);
                                
                                                                }
                                
                                                                
                                
                                                                // 根据音频格式设置编码器
                                if (config.codec === 'aac') {
                                    fallbackOptions.push('-c:a', 'aac');
                                    if (ffmpegConfig.aacBitrate && ffmpegConfig.aacBitrate !== 'N/A') {
                                        fallbackOptions.push('-b:a', ffmpegConfig.aacBitrate);
                                    } else {
                                        fallbackOptions.push('-b:a', '192k');
                                    }
                                } else if (config.codec === 'libmp3lame') {
                                    fallbackOptions.push('-c:a', 'libmp3lame');
                                    if (ffmpegConfig.mp3Bitrate && ffmpegConfig.mp3Bitrate !== 'N/A') {
                                        fallbackOptions.push('-b:a', ffmpegConfig.mp3Bitrate);
                                    } else {
                                        fallbackOptions.push('-b:a', '192k');
                                    }
                                } else if (config.codec === 'libvorbis') {
                                    fallbackOptions.push('-c:a', 'libvorbis');
                                    if (ffmpegConfig.vorbisBitrate && ffmpegConfig.vorbisBitrate !== 'N/A') {
                                        fallbackOptions.push('-b:a', ffmpegConfig.vorbisBitrate);
                                    } else {
                                        fallbackOptions.push('-b:a', '192k');
                                    }
                                } else if (config.codec.startsWith('pcm_')) {
                                    fallbackOptions.push('-c:a', config.codec);
                                } else if (config.codec === 'flac') {
                                    fallbackOptions.push('-c:a', 'flac');
                                    if (ffmpegConfig.flacBitDepth) {
                                        if (ffmpegConfig.flacBitDepth === 'flac_s16') {
                                            fallbackOptions.push('-sample_fmt', 's16');
                                        } else if (ffmpegConfig.flacBitDepth === 'flac_s24' || ffmpegConfig.flacBitDepth === 'flac_s32') {
                                            fallbackOptions.push('-sample_fmt', 's32');
                                        }
                                    }
                                }
                                
                                // 重试使用下一个容器
                                ffmpegCommand = ffmpeg(inputFile)
                                    .outputOptions(fallbackOptions)
                                    .output(outputFile);
                                
                                console.log(`📝 FFmpeg回退命令: ffmpeg -i "${inputFile}" ${fallbackOptions.join(' ')} "${outputFile}"`);
                                
                                ffmpegCommand
                                    .on('progress', (progress) => {
                                        if (progress.percent) {
                                            console.log(`   进度: ${progress.percent.toFixed(1)}%`);
                                        }
                                    })
                                    .on('end', () => {
                                        console.log(`✅ ${config.name} (${container}) 转换完成`);
                                        resolve();
                                    })
                                    .on('error', (err2) => {
                                        fs.unlink(outputFile, (unlinkErr) => {
                                            if (unlinkErr) {
                                                console.error(`删除失败文件失败:`, unlinkErr.message);
                                            }
                                        });
                                        
                                        // 输出第一次回退失败的错误
                                        console.error(`❌ ${config.name} (${container}) 转换失败:`, err2.message);
                                        
                                        // 第二次回退也失败,检查是否还有第三次回退
                                        // 此时container已经是第一次回退的容器了
                                        let secondFallbackIndex = -1;
                                        if (container !== originalContainer) {
                                            secondFallbackIndex = fallbackSequence.indexOf(container);
                                        }
                                        
                                        if (secondFallbackIndex < fallbackSequence.length - 1) {
                                            const thirdContainer = fallbackSequence[secondFallbackIndex + 1];
                                            console.log(`⚠️ ${config.name} ${container}容器转换失败，尝试使用${thirdContainer}容器...`);
                                            outputFile = path.join(outputDir, `split_${config.codec}_${timestamp}_${originalFileName}.${thirdContainer}`);
                                            container = thirdContainer;
                                            finalContainer = thirdContainer;
                                            
                                            ffmpegCommand = ffmpeg(inputFile)
                                                .outputOptions(fallbackOptions)
                                                .output(outputFile);
                                            
                                            console.log(`📝 FFmpeg回退命令: ffmpeg -i "${inputFile}" ${fallbackOptions.join(' ')} "${outputFile}"`);
                                            
                                            ffmpegCommand
                                                .on('progress', (progress) => {
                                                    if (progress.percent) {
                                                        console.log(`   进度: ${progress.percent.toFixed(1)}%`);
                                                    }
                                                })
                                                .on('end', () => {
                                                    console.log(`✅ ${config.name} (${container}) 转换完成`);
                                                    resolve();
                                                })
                                                .on('error', (err3) => {
                                                    fs.unlink(outputFile, (unlinkErr) => {
                                                        if (unlinkErr) {
                                                            console.error(`删除失败文件失败:`, unlinkErr.message);
                                                        }
                                                    });
                                                    console.error(`❌ ${config.name} (${container}) 转换失败:`, err3.message);
                                                    // 第三次也失败,通过resolve一个特殊对象来表示最终失败
                                                    resolve({ failed: true, error: err3.message, container });
                                                })
                                                .run();
                                        } else {
                                            // 没有更多回退了,通过resolve特殊对象表示最终失败
                                            resolve({ failed: true, error: err2.message, container });
                                        }
                                    })
                                    .run();
                            } else {
                                console.error(`❌ ${config.name} (${container}) 转换失败:`, err.message);
                                reject(err);
                            }
                        })
                        .run();
                }).then((result) => {
                    // 检查是否是特殊标记的失败结果
                    if (result && result.failed) {
                        // 已经输出过错误信息了,不需要再处理
                        return;
                    }
                    
                    // 正常转换成功
                    generatedFiles.push({
                        name: `split_${config.codec}_${timestamp}_${originalFileName}.${finalContainer}`,
                        path: `output/split_${config.codec}_${timestamp}_${originalFileName}.${finalContainer}`,
                        description: config.name
                    });
                }).catch((error) => {
                    // 其他类型的错误
                    console.error(`❌ ${config.name} 转换异常:`, error.message);
                });

                generatedFiles.push({
                    name: `split_${config.codec}_${timestamp}_${originalFileName}.${finalContainer}`,
                    path: `output/split_${config.codec}_${timestamp}_${originalFileName}.${finalContainer}`,
                    description: config.name
                });

            } catch (error) {
                console.error(`❌ ${config.name} 转换失败:`, error.message);
            }
        }

        // 清理临时文件
        if (fs.existsSync(inputFile)) {
            fs.unlinkSync(inputFile);
            console.log('🗑️  临时文件已清理');
        }

        console.log('🎉 所有转换完成！');
        res.json({
            success: true,
            files: generatedFiles
        });

    } catch (error) {
        console.error('❌ 转换错误:', error);
        res.json({ success: false, message: error.message });
    }
});

// 获取文件列表 API
app.get('/api/files', (req, res) => {
    try {
        const files = [];
        
        if (fs.existsSync(outputDir)) {
            const fileNames = fs.readdirSync(outputDir);
            
            fileNames.forEach(fileName => {
                const filePath = path.join(outputDir, fileName);
                const stats = fs.statSync(filePath);
                
                // 获取文件大小（MB）
                const fileSize = (stats.size / 1024 / 1024).toFixed(2);
                
                // 获取创建时间
                const createTime = stats.mtime.toLocaleString('zh-CN');
                
                // 解析文件格式
                let format = '未知';
                if (fileName.includes('aac')) format = 'AAC';
                else if (fileName.includes('libmp3lame')) format = 'MP3';
                else if (fileName.includes('libvorbis')) format = 'Vorbis';
                else if (fileName.includes('pcm_s16le')) format = 'PCM';
                else if (fileName.includes('flac')) format = 'FLAC';
                
                files.push({
                    name: fileName,
                    path: `output/${fileName}`,
                    size: fileSize,
                    createTime: createTime,
                    format: format
                });
            });
        }
        
        // 按创建时间倒序排列
        files.sort((a, b) => new Date(b.createTime) - new Date(a.createTime));
        
        res.json({
            success: true,
            files: files,
            total: files.length
        });
    } catch (error) {
        console.error('获取文件列表错误:', error);
        res.json({ success: false, message: error.message });
    }
});

// 删除文件 API
app.delete('/api/files/:filename', (req, res) => {
    try {
        const filename = req.params.filename;
        const filePath = path.join(outputDir, filename);
        
        // 安全检查：防止路径遍历攻击
        if (!filename || filename.includes('..') || filename.includes('/')) {
            return res.json({ success: false, message: '无效的文件名' });
        }
        
        if (!fs.existsSync(filePath)) {
            return res.json({ success: false, message: '文件不存在' });
        }
        
        fs.unlinkSync(filePath);
        console.log(`🗑️ 已删除文件: ${filename}`);
        
        res.json({ success: true, message: '文件删除成功' });
    } catch (error) {
        console.error('删除文件错误:', error);
        res.json({ success: false, message: error.message });
    }
});

// 批量删除文件 API
app.post('/api/files/batch-delete', express.json(), (req, res) => {
    try {
        const { filenames } = req.body;
        
        if (!Array.isArray(filenames) || filenames.length === 0) {
            return res.json({ success: false, message: '请选择要删除的文件' });
        }
        
        let deletedCount = 0;
        const errors = [];
        
        filenames.forEach(filename => {
            try {
                // 安全检查
                if (!filename || filename.includes('..') || filename.includes('/')) {
                    errors.push(`${filename}: 无效的文件名`);
                    return;
                }
                
                const filePath = path.join(outputDir, filename);
                
                if (fs.existsSync(filePath)) {
                    fs.unlinkSync(filePath);
                    deletedCount++;
                    console.log(`🗑️ 已删除文件: ${filename}`);
                } else {
                    errors.push(`${filename}: 文件不存在`);
                }
            } catch (error) {
                errors.push(`${filename}: ${error.message}`);
            }
        });
        
        res.json({
            success: true,
            deletedCount: deletedCount,
            errors: errors
        });
    } catch (error) {
        console.error('批量删除错误:', error);
        res.json({ success: false, message: error.message });
    }
});

// 清空所有文件 API
app.delete('/api/files', (req, res) => {
    try {
        if (!fs.existsSync(outputDir)) {
            return res.json({ success: false, message: 'output 文件夹不存在' });
        }
        
        const fileNames = fs.readdirSync(outputDir);
        let deletedCount = 0;
        
        fileNames.forEach(fileName => {
            const filePath = path.join(outputDir, fileName);
            try {
                fs.unlinkSync(filePath);
                deletedCount++;
            } catch (error) {
                console.error(`删除 ${fileName} 失败:`, error.message);
            }
        });
        
        console.log(`🗑️ 已清空 ${deletedCount} 个文件`);
        
        res.json({
            success: true,
            message: `已清空 ${deletedCount} 个文件`,
            deletedCount: deletedCount
        });
    } catch (error) {
        console.error('清空文件错误:', error);
        res.json({ success: false, message: error.message });
    }
});

// SSL证书配置
const sslOptions = {
    key: fs.readFileSync(path.join(__dirname, 'key.pem')),
    cert: fs.readFileSync(path.join(__dirname, 'cert.pem'))
};

// 启动服务器前检查ffmpeg
checkFFmpeg().then(() => {
    // 启动HTTPS服务器
    const server = https.createServer(sslOptions, app);
    server.timeout = 0; // 无超时限制

// 同时监听IPv4和IPv6
    server.listen(PORT, '0.0.0.0', () => {
        // 获取本机IPv4地址
        const interfaces = require('os').networkInterfaces();
        let ipv4Address = 'localhost';
        
        // 优先级策略：WLAN > Wi-Fi > wlan > WiFi > 其他非内部网卡
        const interfacePriority = ['WLAN', 'Wi-Fi', 'wlan', 'WiFi', 'wlan0', 'wlan1', 'wlan2'];
        
        // 先尝试按优先级查找
        for (const priorityName of interfacePriority) {
            if (interfaces[priorityName]) {
                for (const iface of interfaces[priorityName]) {
                    if (iface.family === 'IPv4' && !iface.internal) {
                        ipv4Address = iface.address;
                        break;
                    }
                }
                if (ipv4Address !== 'localhost') break;
            }
        }
        
        // 如果没找到，使用第一个非内部IPv4地址
        if (ipv4Address === 'localhost') {
            for (const name of Object.keys(interfaces)) {
                for (const iface of interfaces[name]) {
                    if (iface.family === 'IPv4' && !iface.internal) {
                        ipv4Address = iface.address;
                        break;
                    }
                }
                if (ipv4Address !== 'localhost') break;
            }
        }
        
        console.log('='.repeat(50));
        console.log('本地视频播放器 - HTTPS服务器（Node.js + FFmpeg）');
        console.log('='.repeat(50));
        console.log(`📱 请在浏览器中打开以下地址:`);
        console.log(`   本地访问: https://localhost:${PORT}/test-split-manual.html`);
        console.log(`   局域网访问: https://${ipv4Address}:${PORT}/test-split-manual.html`);
        console.log('='.repeat(50));
        console.log('⚠️  注意：首次访问需要信任自签名证书');
        console.log('   点击 "高级" -> "继续访问" 或 "接受风险"');
        console.log('='.repeat(50));
        console.log(`✓ HTTPS服务器已启动，监听端口 ${PORT}`);
        console.log(`✓ IPv4地址访问（不推荐 可能会消耗流量）: https://${ipv4Address}:${PORT}/test-split-manual.html`);
        console.log('--> 推荐本地访问，断网都可以上传转换视频 <--');
        console.log(`✓ 本地访问（推荐 不消耗流量）: https://localhost:${PORT}/test-split-manual.html`);
        console.log(`✓ 无超时限制，支持任意大小文件上传`);
        console.log();
    });
}).catch((error) => {
    console.error('服务器启动失败:', error.message);
    process.exit(1);
});