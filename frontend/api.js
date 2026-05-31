/**
 * ApiClient - REST API 客户端
 * 对接 ESP32 后端 HTTP API
 * 支持：超时控制、错误处理、JSON 通信
 */

class ApiClient {
    constructor(baseUrl = '') {
        this.baseUrl = baseUrl || '';
        this.defaultTimeout = 10000; // 10s
    }

    /**
     * 通用请求方法
     */
    async request(method, path, data = null, options = {}) {
        const url = this.baseUrl + path;
        const timeout = options.timeout || this.defaultTimeout;

        const config = {
            method: method,
            headers: {
                'Content-Type': 'application/json',
            },
        };

        if (data !== null && method !== 'GET') {
            config.body = JSON.stringify(data);
        }

        // 超时控制
        const controller = new AbortController();
        config.signal = controller.signal;
        const timer = setTimeout(() => controller.abort(), timeout);

        try {
            const response = await fetch(url, config);
            clearTimeout(timer);

            if (!response.ok) {
                const errorText = await response.text().catch(() => '');
                throw new Error(`HTTP ${response.status}: ${errorText || response.statusText}`);
            }

            const contentType = response.headers.get('content-type') || '';
            if (contentType.includes('application/json')) {
                return await response.json();
            }
            return await response.text();
        } catch (e) {
            clearTimeout(timer);
            if (e.name === 'AbortError') {
                throw new Error(`请求超时 (${timeout}ms): ${method} ${path}`);
            }
            throw e;
        }
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

    async uploadFile(path, file) {
        const url = this.baseUrl + '/api/files/upload?path=' + encodeURIComponent(path);
        const formData = new FormData();
        formData.append('file', file);

        const controller = new AbortController();
        const timer = setTimeout(() => controller.abort(), 60000); // 上传超时 60s

        try {
            const response = await fetch(url, {
                method: 'POST',
                body: formData,
                signal: controller.signal
            });
            clearTimeout(timer);

            if (!response.ok) {
                throw new Error(`HTTP ${response.status}: ${response.statusText}`);
            }
            return await response.json();
        } catch (e) {
            clearTimeout(timer);
            throw e;
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
            this.getConfig()
        ]);

        return {
            radarStatus: results[0].status === 'fulfilled' ? results[0].value : null,
            systemInfo: results[1].status === 'fulfilled' ? results[1].value : null,
            serverStatus: results[2].status === 'fulfilled' ? results[2].value : null,
            config: results[3].status === 'fulfilled' ? results[3].value : null,
            errors: results
                .map((r, i) => r.status === 'rejected' ? ['radarStatus', 'systemInfo', 'serverStatus', 'config'][i] : null)
                .filter(Boolean)
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
