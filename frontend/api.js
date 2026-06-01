/**
 * ApiClient - REST API 客户端
 * 对接 ESP32 后端 HTTP API
 * 支持：超时控制、错误处理、JSON 通信、请求取消
 */

// 错误类型枚举
const ApiErrorType = {
    NETWORK: 'network',           // 网络错误（离线、DNS 失败等）
    TIMEOUT: 'timeout',           // 请求超时
    HTTP: 'http',                 // HTTP 错误（4xx, 5xx）
    PARSE: 'parse',               // 响应解析错误
    ABORT: 'abort',               // 请求被取消
    UNKNOWN: 'unknown'            // 未知错误
};

// 自定义 API 错误类
class ApiError extends Error {
    constructor(type, message, status = null, originalError = null) {
        super(message);
        this.name = 'ApiError';
        this.type = type;
        this.status = status;
        this.originalError = originalError;
    }
}

class ApiClient {
    constructor(baseUrl = '') {
        this.baseUrl = baseUrl || '';
        this.defaultTimeout = 10000; // 10s
        this.pendingRequests = new Map(); // 跟踪进行中的请求
    }

    /**
     * 通用请求方法
     * @param {string} method - HTTP 方法
     * @param {string} path - 请求路径
     * @param {object} data - 请求数据
     * @param {object} options - 选项 { timeout, signal }
     * @returns {Promise} 响应数据
     */
    async request(method, path, data = null, options = {}) {
        const url = this.baseUrl + path;
        const timeout = options.timeout || this.defaultTimeout;
        const requestId = `${method}:${path}:${Date.now()}`;

        const config = {
            method: method,
            headers: {
                'Content-Type': 'application/json',
            },
        };

        if (data !== null && method !== 'GET') {
            config.body = JSON.stringify(data);
        }

        // 超时控制和外部取消支持
        const controller = new AbortController();
        config.signal = controller.signal;

        // 如果外部提供了 signal，监听其 abort 事件
        if (options.signal) {
            options.signal.addEventListener('abort', () => controller.abort());
        }

        const timer = setTimeout(() => controller.abort(), timeout);

        // 跟踪请求
        this.pendingRequests.set(requestId, { controller, timer });

        try {
            const response = await fetch(url, config);
            clearTimeout(timer);

            if (!response.ok) {
                const errorText = await response.text().catch(() => '');
                throw new ApiError(
                    ApiErrorType.HTTP,
                    `HTTP ${response.status}: ${errorText || response.statusText}`,
                    response.status
                );
            }

            const contentType = response.headers.get('content-type') || '';
            if (contentType.includes('application/json')) {
                return await response.json();
            }
            return await response.text();
        } catch (e) {
            clearTimeout(timer);

            if (e instanceof ApiError) {
                throw e;
            }

            // 分类错误
            if (e.name === 'AbortError') {
                // 判断是超时还是被外部取消
                if (options.signal?.aborted) {
                    throw new ApiError(ApiErrorType.ABORT, `请求被取消: ${method} ${path}`, null, e);
                }
                throw new ApiError(ApiErrorType.TIMEOUT, `请求超时 (${timeout}ms): ${method} ${path}`, null, e);
            }

            if (e.name === 'TypeError' && !navigator.onLine) {
                throw new ApiError(ApiErrorType.NETWORK, '网络离线，请检查网络连接', null, e);
            }

            if (e.name === 'SyntaxError') {
                throw new ApiError(ApiErrorType.PARSE, '响应解析失败', null, e);
            }

            throw new ApiError(ApiErrorType.UNKNOWN, e.message || '未知错误', null, e);
        } finally {
            this.pendingRequests.delete(requestId);
        }
    }

    /**
     * 取消所有进行中的请求
     */
    cancelAllRequests() {
        for (const [id, { controller }] of this.pendingRequests) {
            controller.abort();
            this.pendingRequests.delete(id);
        }
    }

    /**
     * 创建可取消的请求
     * @returns {object} { request: Promise, cancel: function }
     */
    createCancellableRequest(method, path, data = null, options = {}) {
        const controller = new AbortController();
        const request = this.request(method, path, data, { ...options, signal: controller.signal });
        return {
            request,
            cancel: () => controller.abort()
        };
    }

    // ===== 雷达状态与配置 =====

    async getRadarStatus() {
        return this.request('GET', '/api/radar/status');
    }

    async getInstallMode() {
        return this.request('GET', '/api/radar/install_mode');
    }

    async setInstallMode(mode, height, angle) {
        return this.request('PUT', '/api/radar/install_mode', {
            mode: mode,
            height: height || 2.5,
            angle: angle || 0
        });
    }

    async getRange() {
        return this.request('GET', '/api/radar/range');
    }

    async setRange(distance, angleStart, angleEnd) {
        return this.request('PUT', '/api/radar/range', {
            distance: distance,
            angle_start: angleStart,
            angle_end: angleEnd
        });
    }

    // ===== 系统信息 =====

    async getStatus() {
        return this.request('GET', '/api/status');
    }

    async getSystemInfo() {
        return this.request('GET', '/api/system/info');
    }

    // ===== 配置管理 =====

    async getConfig() {
        return this.request('GET', '/api/config');
    }

    async updateConfig(config) {
        return this.request('PUT', '/api/config', config);
    }

    // ===== 日志 =====

    async getLogs(options = {}) {
        const params = new URLSearchParams();
        if (options.count) params.set('count', options.count);
        if (options.level) params.set('level', options.level);
        const query = params.toString();
        return this.request('GET', '/api/logs' + (query ? '?' + query : ''));
    }

    // ===== 文件管理 =====

    async getFileList(path = '/storage/www') {
        return this.request('GET', '/api/files/list?path=' + encodeURIComponent(path));
    }

    async uploadFile(path, file, options = {}) {
        const url = this.baseUrl + '/api/files/upload?path=' + encodeURIComponent(path);
        const formData = new FormData();
        formData.append('file', file);

        const timeout = options.timeout || 60000; // 上传超时 60s
        const controller = new AbortController();

        // 如果外部提供了 signal，监听其 abort 事件
        if (options.signal) {
            options.signal.addEventListener('abort', () => controller.abort());
        }

        const timer = setTimeout(() => controller.abort(), timeout);

        try {
            const response = await fetch(url, {
                method: 'POST',
                body: formData,
                signal: controller.signal
            });
            clearTimeout(timer);

            if (!response.ok) {
                throw new ApiError(
                    ApiErrorType.HTTP,
                    `HTTP ${response.status}: ${response.statusText}`,
                    response.status
                );
            }
            return await response.json();
        } catch (e) {
            clearTimeout(timer);

            if (e instanceof ApiError) {
                throw e;
            }

            if (e.name === 'AbortError') {
                if (options.signal?.aborted) {
                    throw new ApiError(ApiErrorType.ABORT, '上传被取消', null, e);
                }
                throw new ApiError(ApiErrorType.TIMEOUT, `上传超时 (${timeout}ms)`, null, e);
            }

            throw new ApiError(ApiErrorType.UNKNOWN, e.message || '上传失败', null, e);
        }
    }

    async deleteFile(path) {
        return this.request('DELETE', '/api/files/delete?path=' + encodeURIComponent(path));
    }

    // ===== 文件系统 =====

    async getFsInfo() {
        return this.request('GET', '/api/fs/info');
    }

    async formatStorage() {
        return this.request('POST', '/api/fs/format');
    }

    // ===== 批量初始化 =====

    /**
     * 并行获取初始状态
     */
    async getInitialState() {
        const results = await Promise.allSettled([
            this.getRadarStatus(),
            this.getSystemInfo(),
            this.getStatus(),
            this.getConfig()  // 可能 404，配置 API 尚未实现
        ]);

        const errors = results
            .map((r, i) => {
                if (r.status === 'rejected') {
                    const name = ['radarStatus', 'systemInfo', 'serverStatus', 'config'][i];
                    // config 404 是预期行为，不视为错误
                    if (name === 'config' && r.reason && r.reason.status === 404) {
                        return null;
                    }
                    return name;
                }
                return null;
            })
            .filter(Boolean);

        return {
            radarStatus: results[0].status === 'fulfilled' ? results[0].value : null,
            systemInfo: results[1].status === 'fulfilled' ? results[1].value : null,
            serverStatus: results[2].status === 'fulfilled' ? results[2].value : null,
            config: results[3].status === 'fulfilled' ? results[3].value : null,
            errors: errors
        };
    }
}

// ===== 工具函数 =====

function formatBytes(bytes) {
    if (bytes < 1024) return bytes + ' B';
    if (bytes < 1024 * 1024) return (bytes / 1024).toFixed(1) + ' KB';
    return (bytes / (1024 * 1024)).toFixed(1) + ' MB';
}

function formatUptime(seconds) {
    const h = Math.floor(seconds / 3600);
    const m = Math.floor((seconds % 3600) / 60);
    const s = Math.floor(seconds % 60);
    return `${h.toString().padStart(2, '0')}:${m.toString().padStart(2, '0')}:${s.toString().padStart(2, '0')}`;
}
