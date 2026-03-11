// WebSocket心跳检测脚本
(function() {
    let heartbeatInterval = null;
    let missedPongs = 0;
    const maxMissedPongs = 3;
    
    function startHeartbeat() {
        if (heartbeatInterval) {
            clearInterval(heartbeatInterval);
        }
        
        // 每30秒发送一次ping
        heartbeatInterval = setInterval(() => {
            if (window.appState && window.appState.websocket && window.appState.websocket.readyState === WebSocket.OPEN) {
                console.log('📡 发送心跳ping');
                window.appState.websocket.send(JSON.stringify({ type: 'ping' }));
                
                missedPongs++;
                if (missedPongs >= maxMissedPongs) {
                    console.error('🚨 心跳检测失败，可能断开连接');
                    // 但不主动结束通话
                }
            }
        }, 30000);
    }
    
    // 等待appState初始化后再设置监听
    function setupWebSocketMonitor() {
        if (!window.appState) {
            setTimeout(setupWebSocketMonitor, 100);
            return;
        }
        
        // 保存原始的websocket设置
        const originalWebSocket = window.appState.websocket;
        
        // 创建一个新的属性描述符
        Object.defineProperty(window.appState, 'websocket', {
            set: function(ws) {
                this._websocket = ws;
                if (ws) {
                    ws.addEventListener('message', (event) => {
                        try {
                            const message = JSON.parse(event.data);
                            if (message.type === 'pong') {
                                console.log('📡 收到心跳pong');
                                missedPongs = 0;
                            }
                        } catch (e) {
                            // 忽略非JSON消息
                        }
                    });
                    
                    startHeartbeat();
                }
            },
            get: function() {
                return this._websocket;
            }
        });
        
        // 如果已经有websocket，重新设置
        if (originalWebSocket) {
            window.appState.websocket = originalWebSocket;
        }
    }
    
    // 延迟设置监听器
    setTimeout(setupWebSocketMonitor, 1000);
    
    console.log('WebSocket心跳检测脚本已加载');
})();