/**
 * WebSocketClient - WebSocket 连接管理
 * 对接 ESP32 后端 ws://<host>/ws
 * 支持：自动重连、心跳检测、消息分发
 */

class WebSocketClient {
    constructor(url, options = {}) {
        this.url = url || `ws://${window.location.host}/ws`;
        this.options = {
            reconnectBaseDelay: 1000,   // 初始重连延迟 1s
            reconnectMaxDelay: 30000,   // 最大重连延迟 30s
            heartbeatInterval: 30000,   // 心跳间隔 30s
            heartbeatTimeout: 30000,    // 心跳超时 30s（延长以避免误断）
            maxReconnectAttempts: 10,   // 最大重连次数
            ...options
        };

        this.ws = null;
        this.state = 'disconnected'; // disconnected | connecting | connected | reconnecting
        this.reconnectAttempt = 0;
        this.reconnectTimer = null;
        this.heartbeatTimer = null;
        this.heartbeatTimeoutTimer = null;  // 心跳超时检测
        this.lastPongTime = 0;              // 上次收到 pong 的时间
        this.messageHandlers = new Map(); // type -> Set<handler>
        this.eventListeners = new Map();  // event -> Set<listener>

        // 统计
        this.stats = {
            messagesReceived: 0,
            messagesSent: 0,
            lastMessageTime: 0,
            connectTime: 0,
            reconnectCount: 0
        };
    }

    /**
     * 建立连接
     */
    connect() {
        if (this.ws && (this.ws.readyState === WebSocket.CONNECTING || this.ws.readyState === WebSocket.OPEN)) {
            return;
        }

        this._setState('connecting');
        console.log(`[WS] 正在连接 ${this.url}...`);

        try {
            this.ws = new WebSocket(this.url);
        } catch (e) {
            console.error('[WS] 创建连接失败:', e);
            this._scheduleReconnect();
            return;
        }

        this.ws.onopen = (event) => this._onOpen(event);
        this.ws.onmessage = (event) => this._onMessage(event);
        this.ws.onclose = (event) => this._onClose(event);
        this.ws.onerror = (event) => this._onError(event);
    }

    /**
     * 连接成功
     */
    _onOpen(event) {
        console.log('[WS] 连接成功');
        this._setState('connected');
        this.reconnectAttempt = 0;
        this.stats.connectTime = Date.now();
        this.stats.reconnectCount++;

        // 发送订阅请求（临时禁用，用于调试连接稳定性）
        // this.send({ type: 'subscribe' });
        console.log('[WS] 自动订阅已禁用（调试模式）');

        // 启动心跳
        this._startHeartbeat();

        this._emit('connected');
    }

    /**
     * 接收消息
     */
    _onMessage(event) {
        this.stats.messagesReceived++;
        this.stats.lastMessageTime = Date.now();

        let data;
        try {
            data = JSON.parse(event.data);
        } catch (e) {
            console.warn('[WS] 消息解析失败:', event.data);
            return;
        }

        const type = data.type || '';

        // 心跳响应
        if (type === 'pong') {
            this.lastPongTime = Date.now();
            // 清除心跳超时定时器
            if (this.heartbeatTimeoutTimer) {
                clearTimeout(this.heartbeatTimeoutTimer);
                this.heartbeatTimeoutTimer = null;
            }
            return;
        }

        // 分发到类型处理器
        const handlers = this.messageHandlers.get(type);
        if (handlers) {
            handlers.forEach(handler => {
                try {
                    handler(data);
                } catch (e) {
                    console.error(`[WS] 消息处理器错误 [${type}]:`, e);
                }
            });
        }

        // 分发到通用处理器
        const allHandlers = this.messageHandlers.get('*');
        if (allHandlers) {
            allHandlers.forEach(handler => {
                try {
                    handler(data);
                } catch (e) {
                    console.error('[WS] 通用处理器错误:', e);
                }
            });
        }
    }

    /**
     * 连接关闭
     */
    _onClose(event) {
        console.log(`[WS] 连接关闭: code=${event.code} reason=${event.reason}`);
        this._stopHeartbeat();

        if (this.state === 'connected') {
            this._setState('disconnected');
            this._emit('disconnected', { code: event.code, reason: event.reason });
        }

        // 非正常关闭时自动重连
        if (event.code !== 1000) {
            this._scheduleReconnect();
        }
    }

    /**
     * 连接错误
     */
    _onError(event) {
        console.error('[WS] 连接错误');
    }

    /**
     * 调度重连（指数退避）
     */
    _scheduleReconnect() {
        if (this.reconnectTimer) return;

        // 检查是否超过最大重连次数
        if (this.reconnectAttempt >= this.options.maxReconnectAttempts) {
            console.error(`[WS] 重连次数超过上限 (${this.options.maxReconnectAttempts})，停止重连`);
            this._setState('disconnected');
            this._emit('reconnect_failed', {
                attempts: this.reconnectAttempt,
                reason: 'max_attempts_exceeded'
            });
            return;
        }

        const delay = Math.min(
            this.options.reconnectBaseDelay * Math.pow(2, this.reconnectAttempt),
            this.options.reconnectMaxDelay
        );

        this.reconnectAttempt++;
        this._setState('reconnecting');

        console.log(`[WS] 将在 ${Math.round(delay / 1000)}s 后重连 (第 ${this.reconnectAttempt}/${this.options.maxReconnectAttempts} 次)`);

        this._emit('reconnecting', {
            attempt: this.reconnectAttempt,
            delay: delay,
            maxAttempts: this.options.maxReconnectAttempts
        });

        this.reconnectTimer = setTimeout(() => {
            this.reconnectTimer = null;
            this.connect();
        }, delay);
    }

    /**
     * 启动心跳
     */
    _startHeartbeat() {
        this._stopHeartbeat();
        this.lastPongTime = Date.now();

        // 心跳发送定时器
        this.heartbeatTimer = setInterval(() => {
            if (this.ws && this.ws.readyState === WebSocket.OPEN) {
                this.send({ type: 'ping' });

                // 设置心跳超时检测
                if (this.heartbeatTimeoutTimer) {
                    clearTimeout(this.heartbeatTimeoutTimer);
                }
                this.heartbeatTimeoutTimer = setTimeout(() => {
                    console.warn('[WS] 心跳超时，强制断开连接');
                    this._emit('heartbeat_timeout', {
                        lastPongTime: this.lastPongTime,
                        timeout: this.options.heartbeatTimeout
                    });
                    // 强制关闭连接，触发重连
                    if (this.ws) {
                        this.ws.close(3000, 'Heartbeat timeout');
                    }
                }, this.options.heartbeatTimeout);
            }
        }, this.options.heartbeatInterval);
    }

    /**
     * 停止心跳
     */
    _stopHeartbeat() {
        if (this.heartbeatTimer) {
            clearInterval(this.heartbeatTimer);
            this.heartbeatTimer = null;
        }
        if (this.heartbeatTimeoutTimer) {
            clearTimeout(this.heartbeatTimeoutTimer);
            this.heartbeatTimeoutTimer = null;
        }
    }

    /**
     * 发送消息
     */
    send(data) {
        if (this.ws && this.ws.readyState === WebSocket.OPEN) {
            const msg = typeof data === 'string' ? data : JSON.stringify(data);
            this.ws.send(msg);
            this.stats.messagesSent++;
        } else {
            console.warn('[WS] 无法发送消息，连接未就绪');
        }
    }

    /**
     * 主动断开（不触发重连）
     */
    disconnect() {
        this._stopHeartbeat();
        if (this.reconnectTimer) {
            clearTimeout(this.reconnectTimer);
            this.reconnectTimer = null;
        }
        this.reconnectAttempt = 0;

        if (this.ws) {
            this.ws.close(1000, 'Client disconnect');
            this.ws = null;
        }

        this._setState('disconnected');
        this._emit('disconnected', { code: 1000, reason: 'Client disconnect' });
    }

    /**
     * 注册消息处理器
     * @param {string} type - 消息类型，'*' 表示所有消息
     * @param {function} handler - 处理函数
     */
    onMessage(type, handler) {
        if (!this.messageHandlers.has(type)) {
            this.messageHandlers.set(type, new Set());
        }
        this.messageHandlers.get(type).add(handler);
    }

    /**
     * 移除消息处理器
     */
    offMessage(type, handler) {
        const handlers = this.messageHandlers.get(type);
        if (handlers) {
            handlers.delete(handler);
        }
    }

    /**
     * 注册事件监听器
     * @param {string} event - connected | disconnected | reconnecting
     * @param {function} listener
     */
    on(event, listener) {
        if (!this.eventListeners.has(event)) {
            this.eventListeners.set(event, new Set());
        }
        this.eventListeners.get(event).add(listener);
    }

    /**
     * 移除事件监听器
     */
    off(event, listener) {
        const listeners = this.eventListeners.get(event);
        if (listeners) {
            listeners.delete(listener);
        }
    }

    /**
     * 触发事件
     */
    _emit(event, data) {
        const listeners = this.eventListeners.get(event);
        if (listeners) {
            listeners.forEach(listener => {
                try {
                    listener(data);
                } catch (e) {
                    console.error(`[WS] 事件处理器错误 [${event}]:`, e);
                }
            });
        }
    }

    /**
     * 设置状态
     */
    _setState(state) {
        const oldState = this.state;
        this.state = state;
        if (oldState !== state) {
            this._emit('stateChange', { oldState, newState: state });
        }
    }

    /**
     * 获取当前状态
     */
    getState() {
        return this.state;
    }

    /**
     * 是否已连接
     */
    isConnected() {
        return this.state === 'connected';
    }
}
