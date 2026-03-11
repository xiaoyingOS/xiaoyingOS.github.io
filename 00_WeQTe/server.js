const http = require('http');
const fs = require('fs');
const path = require('path');
const WebSocket = require('ws');

const port = 8084;

// 存储在线用户和连接
const onlineUsers = new Map(); // userId -> { displayName, ws }
const connections = new Map(); // ws -> userId

// 创建HTTP服务器
const server = http.createServer((req, res) => {
    // 设置CORS头
    res.setHeader('Access-Control-Allow-Origin', '*');
    res.setHeader('Access-Control-Allow-Methods', 'GET, POST, PUT, DELETE');
    res.setHeader('Access-Control-Allow-Headers', 'Content-Type');
    
    // 处理根路径和测试页面
    if (req.url === '/' || req.url === '/index.html') {
        fs.readFile(path.join(__dirname, 'chat.html'), (err, data) => {
            if (err) {
                res.writeHead(500);
                res.end('Error loading file');
                return;
            }
            
            res.writeHead(200, { 'Content-Type': 'text/html' });
            res.end(data);
        });
    } else if (req.url === '/test') {
        fs.readFile(path.join(__dirname, 'test.html'), (err, data) => {
            if (err) {
                res.writeHead(500);
                res.end('Error loading test file');
                return;
            }
            
            res.writeHead(200, { 'Content-Type': 'text/html' });
            res.end(data);
        });
    } else if (req.url === '/chunk-uploader.js') {
        fs.readFile(path.join(__dirname, 'chunk-uploader.js'), (err, data) => {
            if (err) {
                res.writeHead(500);
                res.end('Error loading file');
                return;
            }
            
            res.writeHead(200, { 'Content-Type': 'application/javascript' });
            res.end(data);
        });
    } else if (req.url === '/binary-upload.js') {
        fs.readFile(path.join(__dirname, 'binary-upload.js'), (err, data) => {
            if (err) {
                res.writeHead(500);
                res.end('Error loading file');
                return;
            }
            
            res.writeHead(200, { 'Content-Type': 'application/javascript' });
            res.end(data);
        });
    } else {
        // 处理其他请求
        res.writeHead(404);
        res.end('Not found');
    }
});

// 创建WebSocket服务器
const wss = new WebSocket.Server({ server });

// WebSocket连接处理
wss.on('connection', (ws) => {
    console.log('新的WebSocket连接已建立');
    
    // 连接关闭时的处理
    ws.on('close', () => {
        console.log('WebSocket连接已关闭');
        
        // 获取用户ID
        const userId = connections.get(ws);
        
        if (userId) {
            // 从在线用户列表中移除
            const user = onlineUsers.get(userId);
            if (user) {
                onlineUsers.delete(userId);
                
                // 通知其他用户该用户已下线
                const offlineMessage = {
                    type: 'user_disconnected',
                    data: {
                        userId: userId,
                        displayName: user.displayName
                    }
                };
                
                broadcastToOtherUsers(userId, offlineMessage);
            }
            
            // 清除连接映射
            connections.delete(ws);
        }
    });
    
    // 错误处理
    ws.on('error', (error) => {
        console.error('WebSocket错误:', error);
    });
    
    // 接收消息
    ws.on('message', (message) => {
        try {
            const data = JSON.parse(message);
            handleMessage(ws, data);
        } catch (error) {
            console.error('解析消息失败:', error);
        }
    });
});

// 处理客户端消息
function handleMessage(ws, message) {
    const userId = connections.get(ws);
    
    switch (message.type) {
        case 'user_online':
            // 用户上线
            if (message.data.userId) {
                // 添加到在线用户列表
                onlineUsers.set(message.data.userId, {
                    displayName: message.data.displayName,
                    ws: ws
                });
                
                // 添加连接映射
                connections.set(ws, message.data.userId);
                
                // 发送当前在线用户列表给新用户
                const onlineUsersList = Array.from(onlineUsers.entries()).map(([id, user]) => ({
                    id: id,
                    displayName: user.displayName
                }));
                
                ws.send(JSON.stringify({
                    type: 'online_users',
                    data: onlineUsersList
                }));
                
                // 通知其他用户有新用户上线
                const newUserMessage = {
                    type: 'user_connected',
                    data: {
                        userId: message.data.userId,
                        displayName: message.data.displayName
                    }
                };
                
                broadcastToOtherUsers(message.data.userId, newUserMessage);
            }
            break;
            
        case 'connection_request':
            // 连接请求
            if (message.data.targetUserId && message.data.userId) {
                const targetUser = onlineUsers.get(message.data.targetUserId);
                
                if (targetUser && targetUser.ws.readyState === WebSocket.OPEN) {
                    // 转发连接请求给目标用户
                    targetUser.ws.send(JSON.stringify(message));
                } else {
                    // 目标用户不在线
                    ws.send(JSON.stringify({
                        type: 'connection_response',
                        data: {
                            requestId: message.data.requestId,
                            accepted: false,
                            reason: '用户不在线'
                        }
                    }));
                }
            }
            break;
            
        case 'connection_response':
            // 连接响应
            if (message.data.userId) {
                const targetUser = onlineUsers.get(message.data.userId);
                
                if (targetUser && targetUser.ws.readyState === WebSocket.OPEN) {
                    // 转发连接响应给目标用户
                    targetUser.ws.send(JSON.stringify(message));
                }
            }
            break;
            
        case 'chat_message':
            // 聊天消息
            if (message.data.targetUserId && message.data.senderId) {
                const targetUser = onlineUsers.get(message.data.targetUserId);
                
                if (targetUser && targetUser.ws.readyState === WebSocket.OPEN) {
                    // 转发消息给目标用户
                    targetUser.ws.send(JSON.stringify(message));
                }
            }
            break;
            
        case 'connection_ended':
            // 结束连接
            if (message.data.targetUserId && message.data.userId) {
                const targetUser = onlineUsers.get(message.data.targetUserId);
                
                if (targetUser && targetUser.ws.readyState === WebSocket.OPEN) {
                    // 转发结束连接消息给目标用户
                    targetUser.ws.send(JSON.stringify(message));
                }
            }
            break;
            
        case 'voice_call_request':
            // 语音通话请求
            console.log('服务器收到语音通话请求:', message.data);
            if (message.data.targetUserId && message.data.userId) {
                const targetUser = onlineUsers.get(message.data.targetUserId);
                
                if (targetUser && targetUser.ws.readyState === WebSocket.OPEN) {
                    // 转发语音通话请求给目标用户
                    targetUser.ws.send(JSON.stringify(message));
                    console.log(`语音通话请求已转发: ${message.data.userId} -> ${message.data.targetUserId}`);
                } else {
                    // 目标用户不在线
                    console.log(`目标用户不在线: ${message.data.targetUserId}`);
                    ws.send(JSON.stringify({
                        type: 'voice_call_response',
                        data: {
                            requestId: message.data.requestId,
                            accepted: false,
                            reason: '用户不在线'
                        }
                    }));
                }
            }
            break;
            
        case 'video_call_request':
            // 视频通话请求
            console.log('服务器收到视频通话请求:', message.data);
            if (message.data.targetUserId && message.data.userId) {
                const targetUser = onlineUsers.get(message.data.targetUserId);
                
                if (targetUser && targetUser.ws.readyState === WebSocket.OPEN) {
                    // 转发视频通话请求给目标用户
                    targetUser.ws.send(JSON.stringify(message));
                    console.log(`视频通话请求已转发: ${message.data.userId} -> ${message.data.targetUserId}`);
                } else {
                    // 目标用户不在线
                    console.log(`目标用户不在线: ${message.data.targetUserId}`);
                    ws.send(JSON.stringify({
                        type: 'video_call_response',
                        data: {
                            requestId: message.data.requestId,
                            accepted: false,
                            reason: '用户不在线'
                        }
                    }));
                }
            }
            break;
            
        case 'voice_call_response':
            // 语音通话响应
            if (message.data.userId) {
                const targetUser = onlineUsers.get(message.data.userId);
                
                if (targetUser && targetUser.ws.readyState === WebSocket.OPEN) {
                    // 转发语音通话响应给目标用户
                    targetUser.ws.send(JSON.stringify(message));
                    console.log(`语音通话响应: ${message.data.userId} -> ${connections.get(ws)}`);
                }
            }
            break;
            
        case 'video_call_response':
            // 视频通话响应
            if (message.data.userId) {
                const targetUser = onlineUsers.get(message.data.userId);
                
                if (targetUser && targetUser.ws.readyState === WebSocket.OPEN) {
                    // 转发视频通话响应给目标用户
                    targetUser.ws.send(JSON.stringify(message));
                    console.log(`视频通话响应: ${message.data.userId} -> ${connections.get(ws)}`);
                }
            }
            break;
            
        case 'call_ended':
            // 通话结束
            if (message.data.targetUserId && message.data.userId) {
                const targetUser = onlineUsers.get(message.data.targetUserId);
                
                if (targetUser && targetUser.ws.readyState === WebSocket.OPEN) {
                    // 转发通话结束消息给目标用户
                    targetUser.ws.send(JSON.stringify(message));
                    console.log(`通话结束: ${message.data.userId} -> ${message.data.targetUserId}`);
                }
            }
            break;
            
        case 'webrtc_offer':
            // WebRTC Offer
            if (message.data.targetUserId && message.data.userId) {
                const targetUser = onlineUsers.get(message.data.targetUserId);
                
                if (targetUser && targetUser.ws.readyState === WebSocket.OPEN) {
                    // 转发WebRTC Offer给目标用户
                    targetUser.ws.send(JSON.stringify(message));
                    console.log(`WebRTC Offer: ${message.data.userId} -> ${message.data.targetUserId}`);
                }
            }
            break;
            
        case 'webrtc_answer':
            // WebRTC Answer
            if (message.data.targetUserId && message.data.userId) {
                const targetUser = onlineUsers.get(message.data.targetUserId);
                
                if (targetUser && targetUser.ws.readyState === WebSocket.OPEN) {
                    // 转发WebRTC Answer给目标用户
                    targetUser.ws.send(JSON.stringify(message));
                    console.log(`WebRTC Answer: ${message.data.userId} -> ${message.data.targetUserId}`);
                }
            }
            break;
            
        case 'webrtc_ice_candidate':
            // WebRTC ICE Candidate
            if (message.data.targetUserId && message.data.userId) {
                const targetUser = onlineUsers.get(message.data.targetUserId);
                
                if (targetUser && targetUser.ws.readyState === WebSocket.OPEN) {
                    // 转发WebRTC ICE Candidate给目标用户
                    targetUser.ws.send(JSON.stringify(message));
                }
            }
            break;
            
        case 'user_offline':
            // 用户主动下线（页面关闭时）
            if (userId) {
                // 从在线用户列表中移除
                const user = onlineUsers.get(userId);
                if (user) {
                    onlineUsers.delete(userId);
                    
                    // 通知其他用户该用户已下线
                    const offlineMessage = {
                        type: 'user_disconnected',
                        data: {
                            userId: userId,
                            displayName: user.displayName
                        }
                    };
                    
                    broadcastToOtherUsers(userId, offlineMessage);
                }
                
                // 清除连接映射
                connections.delete(ws);
            }
            break;
            
        default:
            console.log('未知消息类型:', message.type);
    }
}

// 向除指定用户外的所有用户广播消息
function broadcastToOtherUsers(excludeUserId, message) {
    const messageStr = JSON.stringify(message);
    
    onlineUsers.forEach((user, userId) => {
        if (userId !== excludeUserId && user.ws.readyState === WebSocket.OPEN) {
            user.ws.send(messageStr);
        }
    });
}

server.listen(port, '0.0.0.0', () => {
    console.log(`聊天应用服务器已启动`);
    console.log(`访问地址: http://localhost:${port}/`);
    console.log(`手机访问: http://0.0.0.0:${port}/`);
    console.log(`WebSocket服务: ws://localhost:${port}/`);
    console.log(`\n=========================================`);
    console.log(`语音视频功能使用说明：`);
    console.log(`1. 直接打开HTML文件（推荐）：`);
    console.log(`   - 在文件管理器中找到 chat.html`);
    console.log(`   - 双击打开，语音视频功能完全可用`);
    console.log(`\n2. 通过HTTP访问（可能受限）：`);
    console.log(`   - 浏览器可能阻止访问摄像头和麦克风`);
    console.log(`   - 如需使用，请尝试在浏览器设置中允许不安全内容的媒体访问`);
    console.log(`\n=========================================`);
    console.log(`网络聊天功能已启用：`);
    console.log(`- 用户可以实时查看在线用户列表`);
    console.log(`- 通过用户ID连接到真实用户`);
    console.log(`- 实时发送和接收文本、图片、视频、文件和语音消息`);
    console.log(`=========================================`);
});