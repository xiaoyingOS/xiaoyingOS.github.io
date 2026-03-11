// 50秒断开问题调试脚本
(function() {
    const debugLog = [];
    let callStartTime = null;
    let isMonitoring = false;
    
    function log(message, data = null) {
        const timestamp = Date.now();
        const elapsed = callStartTime ? timestamp - callStartTime : 0;
        const entry = {
            timestamp: timestamp,
            elapsed: elapsed,
            message: message,
            data: data
        };
        debugLog.push(entry);
        
        const logStr = `[${new Date(timestamp).toLocaleTimeString()}] [${elapsed}ms] ${message}`;
        console.log('🔍 DEBUG:', logStr, data || '');
        
        // 检查是否接近50秒
        if (elapsed > 45000 && elapsed < 55000) {
            console.error('🚨 接近50秒断开点！', message, data);
        }
    }
    
    // 监控endCall调用
    const originalEndCall = window.endCall;
    window.endCall = function() {
        log('endCall被调用', {
            stack: new Error().stack,
            isInCall: window.appState?.isInCall,
            callType: window.appState?.callType
        });
        
        const elapsed = callStartTime ? Date.now() - callStartTime : 0;
        if (elapsed > 45000 && elapsed < 55000) {
            console.error('🚨 50秒断开确认！endCall在', elapsed, 'ms时被调用');
        }
        
        return originalEndCall.apply(this, arguments);
    };
    
    // 监控terminateConnection调用
    const originalTerminateConnection = window.terminateConnection;
    window.terminateConnection = function() {
        log('terminateConnection被调用', {
            stack: new Error().stack,
            isInCall: window.appState?.isInCall,
            isConnected: window.appState?.isConnected
        });
        return originalTerminateConnection.apply(this, arguments);
    };
    
    // 监控WebSocket消息
    const originalHandleServerMessage = window.handleServerMessage;
    window.handleServerMessage = function(message) {
        if (message.type === 'call_ended' || message.type === 'connection_ended') {
            log('收到服务器消息', {
                type: message.type,
                data: message.data,
                isInCall: window.appState?.isInCall
            });
        }
        return originalHandleServerMessage.apply(this, arguments);
    };
    
    // 监控WebRTC状态变化
    let rtcConnectionMonitor = null;
    
    function startRTCMonitoring() {
        if (rtcConnectionMonitor) {
            clearInterval(rtcConnectionMonitor);
        }
        
        rtcConnectionMonitor = setInterval(() => {
            if (window.appState && window.appState.isInCall && window.appState.rtcPeerConnection) {
                const connectionState = window.appState.rtcPeerConnection.connectionState;
                const iceState = window.appState.rtcPeerConnection.iceConnectionState;
                
                log('WebRTC状态检查', {
                    connectionState: connectionState,
                    iceConnectionState: iceState
                });
                
                if (connectionState === 'failed' || connectionState === 'disconnected' || connectionState === 'closed') {
                    console.error('🚨 WebRTC连接异常！', {
                        connectionState: connectionState,
                        iceState: iceState,
                        elapsed: Date.now() - callStartTime
                    });
                }
            }
        }, 5000);
    }
    
    // 监控通话开始
    const originalStartCall = window.startCall;
    window.startCall = function() {
        callStartTime = Date.now();
        isMonitoring = true;
        log('通话开始监控');
        startRTCMonitoring();
        return originalStartCall.apply(this, arguments);
    };
    
    // 监控acceptCall
    const originalAcceptCall = window.acceptCall;
    window.acceptCall = function() {
        if (!isMonitoring) {
            callStartTime = Date.now();
            isMonitoring = true;
            log('通话开始监控（接听）');
            startRTCMonitoring();
        }
        return originalAcceptCall.apply(this, arguments);
    };
    
    // 导出调试函数
    window.debug50s = {
        log: log,
        getLog: function() {
            return debugLog.slice();
        },
        clearLog: function() {
            debugLog.length = 0;
        },
        exportLog: function() {
            const blob = new Blob([JSON.stringify(debugLog, null, 2)], { type: 'application/json' });
            const url = URL.createObjectURL(blob);
            const a = document.createElement('a');
            a.href = url;
            a.download = `50s-debug-${Date.now()}.json`;
            a.click();
            URL.revokeObjectURL(url);
        }
    };
    
    log('50秒断开调试脚本已加载');
})();