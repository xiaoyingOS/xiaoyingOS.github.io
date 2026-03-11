// 添加详细的通话状态监控
(function() {
    // 创建一个监控对象来跟踪所有可能断开通话的事件
    const callMonitor = {
        startTime: null,
        events: [],
        
        log: function(event, details) {
            const timestamp = Date.now();
            const elapsed = this.startTime ? timestamp - this.startTime : 0;
            const logEntry = {
                timestamp: timestamp,
                elapsed: elapsed,
                event: event,
                details: details || ''
            };
            this.events.push(logEntry);
            console.log(`📞 [${elapsed}ms] ${event}:`, details);
            
            // 如果接近50秒，特别标记
            if (elapsed > 45000 && elapsed < 55000) {
                console.error('🚨 接近50秒断开点！事件:', event, details);
            }
        },
        
        start: function() {
            this.startTime = Date.now();
            this.events = [];
            this.log('通话开始');
        },
        
        end: function(reason) {
            this.log('通话结束', reason);
            console.log('📞 通话事件历史:', this.events);
        }
    };
    
    // 监控所有可能导致断开的事件
    const originalEndCall = window.endCall;
    window.endCall = function() {
        callMonitor.log('endCall被调用', new Error().stack);
        return originalEndCall.apply(this, arguments);
    };
    
    const originalTerminateConnection = window.terminateConnection;
    window.terminateConnection = function() {
        callMonitor.log('terminateConnection被调用', new Error().stack);
        return originalTerminateConnection.apply(this, arguments);
    };
    
    // 监控WebSocket事件
    const originalHandleServerMessage = window.handleServerMessage;
    window.handleServerMessage = function(message) {
        if (message.type === 'call_ended' || message.type === 'connection_ended') {
            callMonitor.log('收到服务器消息', message.type);
        }
        return originalHandleServerMessage.apply(this, arguments);
    };
    
    // 监控WebRTC事件
    window.addEventListener('load', function() {
        // 监控WebRTC连接状态
        setInterval(() => {
            if (window.appState && window.appState.isInCall && window.appState.rtcPeerConnection) {
                const connectionState = window.appState.rtcPeerConnection.connectionState;
                const iceState = window.appState.rtcPeerConnection.iceConnectionState;
                
                if (connectionState === 'failed' || connectionState === 'disconnected' || connectionState === 'closed') {
                    callMonitor.log('WebRTC状态异常', `connection: ${connectionState}, ice: ${iceState}`);
                }
            }
        }, 5000);
    });
    
    // 导出监控对象
    window.callMonitor = callMonitor;
    
    // 在开始通话时自动启动监控
    const originalStartCall = window.startCall;
    window.startCall = function() {
        callMonitor.start();
        return originalStartCall.apply(this, arguments);
    };
})();