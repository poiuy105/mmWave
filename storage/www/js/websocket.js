/**
 * WebSocketClient - WebSocket 客户端类
 * 处理与 ESP32 的实时数据通信
 * 支持自动重连、心跳检测、消息处理
 */
class WebSocketClient {
    constructor(url, options = {}) {
        this.url = url || `ws://${window.location.host}/ws`;
        this.options = {
            reconnectInterval: options.reconnectInterval || 3000,    // 重连间隔
            heartbeatInterval: options.heartbeatInterval || 30000,   // 心跳间隔
            maxReconnectAttempts: options.maxReconnectAttempts || 10, // 最大重连次数
            ...options
        };

        this.ws = null;
        this.isConnected = false;
        this.reconnectAttempts = 0;
        this.reconnectTimer = null;
        this.heartbeatTimer = null;
        this.messageHandlers = new Map();  // 消息处理器
        this.eventListeners = new Map();   // 事件监听器

        // 统计数据
        this.stats = {
            messagesReceived: 0,
            messagesSent: 0,
            bytesReceived: 0,
            bytesSent: 0,
            connectTime: null,
            lastMessageTime: null
        };
    }

    /**
     * 连接到 WebSocket 服务器
     */
    connect() {
        if (this.ws && (this.ws.readyState === WebSocket.CONNECTING || 
                        this.ws.readyState === WebSocket.OPEN)) {
            console.log('[WebSocket] Already connecting or connected');
            return;
        }

        try {
            console.log(`[WebSocket] Connecting to ${this.url}...`);
            this.ws = new WebSocket(this.url);

            this.ws.onopen = (event) => this.handleOpen(event);
            this.ws.onmessage = (event) => this.handleMessage(event);
            this.ws.onclose = (event) => this.handleClose(event);
            this.ws.onerror = (event) => this.handleError(event);

        } catch (error) {
            console.error('[WebSocket] Connection error:', error);
            this.scheduleReconnect();
        }
    }

    /**
     * 处理连接成功
     */
    handleOpen(event) {
        console.log('[WebSocket] Connected');
        this.isConnected = true;
        this.reconnectAttempts = 0;
        this.stats.connectTime = Date.now();

        // 启动心跳
        this.startHeartbeat();

        // 触发连接事件
        this.emit('connected', event);

        // 发送初始订阅消息
        this.send({
            type: 'subscribe',
            channels: ['radar', 'system']
        });
    }

    /**
     * 处理消息接收
     */
    handleMessage(event) {
        this.stats.messagesReceived++;
        this.stats.bytesReceived += event.data.length;
        this.stats.lastMessageTime = Date.now();

        try {
            const message = JSON.parse(event.data);
            
            // 处理心跳响应
            if (message.type === 'pong') {
                return;
            }

            // 处理雷达数据
            if (message.type === 'radar_data') {
                this.emit('radarData', message.data);
            }

            // 处理系统状态
            else if (message.type === 'system_status') {
                this.emit('systemStatus', message.data);
            }

            // 处理日志消息
            else if (message.type === 'log') {
                this.emit('log', message.data);
            }

            // 处理配置更新
            else if (message.type === 'config_update') {
                this.emit('configUpdate', message.data);
            }

            // 处理错误消息
            else if (message.type === 'error') {
                console.error('[WebSocket] Server error:', message.error);
                this.emit('error', message);
            }

            // 通用消息处理
            else {
                this.emit('message', message);
            }

            // 调用特定类型的处理器
            if (message.type && this.messageHandlers.has(message.type)) {
                this.messageHandlers.get(message.type).forEach(handler => {
                    try {
                        handler(message.data, message);
                    } catch (err) {
                        console.error('[WebSocket] Handler error:', err);
                    }
                });
            }

        } catch (error) {
            console.error('[WebSocket] Message parse error:', error);
            this.emit('parseError', { data: event.data, error });
        }
    }

    /**
     * 处理连接关闭
     */
    handleClose(event) {
        console.log(`[WebSocket] Closed: code=${event.code}, reason=${event.reason}`);
        this.isConnected = false;
        this.stopHeartbeat();
        this.emit('disconnected', event);

        // 非主动关闭，尝试重连
        if (!event.wasClean) {
            this.scheduleReconnect();
        }
    }

    /**
     * 处理错误
     */
    handleError(event) {
        console.error('[WebSocket] Error:', event);
        this.emit('error', event);
    }

    /**
     * 安排重连
     */
    scheduleReconnect() {
        if (this.reconnectAttempts >= this.options.maxReconnectAttempts) {
            console.error('[WebSocket] Max reconnect attempts reached');
            this.emit('reconnectFailed');
            return;
        }

        this.reconnectAttempts++;
        console.log(`[WebSocket] Reconnecting in ${this.options.reconnectInterval}ms (attempt ${this.reconnectAttempts})`);

        this.reconnectTimer = setTimeout(() => {
            this.connect();
        }, this.options.reconnectInterval);
    }

    /**
     * 启动心跳
     */
    startHeartbeat() {
        this.heartbeatTimer = setInterval(() => {
            if (this.isConnected) {
                this.send({ type: 'ping', timestamp: Date.now() });
            }
        }, this.options.heartbeatInterval);
    }

    /**
     * 停止心跳
     */
    stopHeartbeat() {
        if (this.heartbeatTimer) {
            clearInterval(this.heartbeatTimer);
            this.heartbeatTimer = null;
        }
    }

    /**
     * 发送消息
     */
    send(data) {
        if (!this.isConnected || !this.ws) {
            console.warn('[WebSocket] Not connected, cannot send');
            return false;
        }

        try {
            const message = typeof data === 'string' ? data : JSON.stringify(data);
            this.ws.send(message);
            this.stats.messagesSent++;
            this.stats.bytesSent += message.length;
            return true;
        } catch (error) {
            console.error('[WebSocket] Send error:', error);
            return false;
        }
    }

    /**
     * 订阅雷达数据
     */
    subscribeRadar() {
        return this.send({
            type: 'subscribe',
            channel: 'radar'
        });
    }

    /**
     * 取消订阅雷达数据
     */
    unsubscribeRadar() {
        return this.send({
            type: 'unsubscribe',
            channel: 'radar'
        });
    }

    /**
     * 请求立即发送一次数据
     */
    requestData() {
        return this.send({
            type: 'request',
            data: 'radar_frame'
        });
    }

    /**
     * 注册消息处理器
     */
    onMessage(type, handler) {
        if (!this.messageHandlers.has(type)) {
            this.messageHandlers.set(type, []);
        }
        this.messageHandlers.get(type).push(handler);

        // 返回取消注册函数
        return () => this.offMessage(type, handler);
    }

    /**
     * 移除消息处理器
     */
    offMessage(type, handler) {
        if (this.messageHandlers.has(type)) {
            const handlers = this.messageHandlers.get(type);
            const index = handlers.indexOf(handler);
            if (index > -1) {
                handlers.splice(index, 1);
            }
        }
    }

    /**
     * 添加事件监听器
     */
    on(event, listener) {
        if (!this.eventListeners.has(event)) {
            this.eventListeners.set(event, []);
        }
        this.eventListeners.get(event).push(listener);

        return () => this.off(event, listener);
    }

    /**
     * 移除事件监听器
     */
    off(event, listener) {
        if (this.eventListeners.has(event)) {
            const listeners = this.eventListeners.get(event);
            const index = listeners.indexOf(listener);
            if (index > -1) {
                listeners.splice(index, 1);
            }
        }
    }

    /**
     * 触发事件
     */
    emit(event, data) {
        if (this.eventListeners.has(event)) {
            this.eventListeners.get(event).forEach(listener => {
                try {
                    listener(data);
                } catch (err) {
                    console.error(`[WebSocket] Event listener error for '${event}':`, err);
                }
            });
        }
    }

    /**
     * 断开连接
     */
    disconnect() {
        // 清除重连定时器
        if (this.reconnectTimer) {
            clearTimeout(this.reconnectTimer);
            this.reconnectTimer = null;
        }

        this.stopHeartbeat();

        if (this.ws) {
            // 正常关闭，不触发重连
            this.ws.close(1000, 'Client disconnect');
            this.ws = null;
        }

        this.isConnected = false;
    }

    /**
     * 获取连接状态
     */
    getStatus() {
        return {
            connected: this.isConnected,
            url: this.url,
            reconnectAttempts: this.reconnectAttempts,
            stats: { ...this.stats },
            readyState: this.ws ? this.ws.readyState : WebSocket.CLOSED
        };
    }

    /**
     * 重置统计数据
     */
    resetStats() {
        this.stats = {
            messagesReceived: 0,
            messagesSent: 0,
            bytesReceived: 0,
            bytesSent: 0,
            connectTime: this.stats.connectTime,
            lastMessageTime: null
        };
    }
}

// 导出
if (typeof module !== 'undefined' && module.exports) {
    module.exports = WebSocketClient;
}
