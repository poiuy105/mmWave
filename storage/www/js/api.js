/**
 * ApiClient - REST API 客户端类
 * 处理与 ESP32 的 HTTP API 通信
 * 支持配置管理、日志获取、系统控制
 */
class ApiClient {
    constructor(baseUrl) {
        this.baseUrl = baseUrl || '';
        this.defaultTimeout = 10000; // 默认超时 10 秒
    }

    /**
     * 构建完整 URL
     */
    buildUrl(path) {
        return `${this.baseUrl}/api${path}`;
    }

    /**
     * 发送 HTTP 请求
     */
    async request(method, path, data = null, options = {}) {
        const url = this.buildUrl(path);
        const timeout = options.timeout || this.defaultTimeout;

        const fetchOptions = {
            method: method,
            headers: {
                'Content-Type': 'application/json',
                'Accept': 'application/json'
            }
        };

        if (data && (method === 'POST' || method === 'PUT' || method === 'PATCH')) {
            fetchOptions.body = JSON.stringify(data);
        }

        // 创建 AbortController 用于超时控制
        const controller = new AbortController();
        fetchOptions.signal = controller.signal;

        const timeoutId = setTimeout(() => controller.abort(), timeout);

        try {
            const response = await fetch(url, fetchOptions);
            clearTimeout(timeoutId);

            if (!response.ok) {
                const error = await response.json().catch(() => ({
                    error: `HTTP ${response.status}: ${response.statusText}`
                }));
                throw new Error(error.error || `HTTP ${response.status}`);
            }

            // 处理空响应
            if (response.status === 204) {
                return null;
            }

            return await response.json();

        } catch (error) {
            clearTimeout(timeoutId);

            if (error.name === 'AbortError') {
                throw new Error('Request timeout');
            }

            throw error;
        }
    }

    // ==================== 配置管理 API ====================

    /**
     * 获取所有配置
     */
    async getConfig() {
        return this.request('GET', '/config');
    }

    /**
     * 获取特定配置项
     */
    async getConfigItem(section, key) {
        return this.request('GET', `/config/${section}/${key}`);
    }

    /**
     * 更新配置
     */
    async updateConfig(config) {
        return this.request('PUT', '/config', config);
    }

    /**
     * 更新特定配置项
     */
    async updateConfigItem(section, key, value) {
        return this.request('PUT', `/config/${section}/${key}`, { value });
    }

    /**
     * 重置配置为默认值
     */
    async resetConfig() {
        return this.request('POST', '/config/reset');
    }

    /**
     * 导出配置
     */
    async exportConfig() {
        return this.request('GET', '/config/export');
    }

    /**
     * 导入配置
     */
    async importConfig(config) {
        return this.request('POST', '/config/import', config);
    }

    // ==================== 雷达控制 API ====================

    /**
     * 获取雷达状态
     */
    async getRadarStatus() {
        return this.request('GET', '/radar/status');
    }

    /**
     * 启动雷达
     */
    async startRadar() {
        return this.request('POST', '/radar/start');
    }

    /**
     * 停止雷达
     */
    async stopRadar() {
        return this.request('POST', '/radar/stop');
    }

    /**
     * 重启雷达
     */
    async restartRadar() {
        return this.request('POST', '/radar/restart');
    }

    /**
     * 获取雷达参数
     */
    async getRadarParams() {
        return this.request('GET', '/radar/params');
    }

    /**
     * 设置雷达参数
     */
    async setRadarParams(params) {
        return this.request('PUT', '/radar/params', params);
    }

    /**
     * 获取检测区域
     */
    async getDetectionRegions() {
        return this.request('GET', '/radar/regions');
    }

    /**
     * 设置检测区域
     */
    async setDetectionRegions(regions) {
        return this.request('PUT', '/radar/regions', regions);
    }

    /**
     * 添加检测区域
     */
    async addDetectionRegion(region) {
        return this.request('POST', '/radar/regions', region);
    }

    /**
     * 删除检测区域
     */
    async deleteDetectionRegion(regionId) {
        return this.request('DELETE', `/radar/regions/${regionId}`);
    }

    // ==================== 日志 API ====================

    /**
     * 获取日志列表
     */
    async getLogs(options = {}) {
        const params = new URLSearchParams();
        if (options.level) params.append('level', options.level);
        if (options.limit) params.append('limit', options.limit);
        if (options.offset) params.append('offset', options.offset);
        if (options.startTime) params.append('start_time', options.startTime);
        if (options.endTime) params.append('end_time', options.endTime);

        const query = params.toString() ? `?${params.toString()}` : '';
        return this.request('GET', `/logs${query}`);
    }

    /**
     * 获取最新日志
     */
    async getLatestLogs(count = 50) {
        return this.request('GET', `/logs/latest?count=${count}`);
    }

    /**
     * 清空日志
     */
    async clearLogs() {
        return this.request('DELETE', '/logs');
    }

    /**
     * 下载日志文件
     */
    downloadLogs() {
        const url = this.buildUrl('/logs/download');
        window.open(url, '_blank');
    }

    // ==================== 系统 API ====================

    /**
     * 获取系统状态
     */
    async getSystemStatus() {
        return this.request('GET', '/system/status');
    }

    /**
     * 获取系统信息
     */
    async getSystemInfo() {
        return this.request('GET', '/system/info');
    }

    /**
     * 重启系统
     */
    async rebootSystem() {
        return this.request('POST', '/system/reboot');
    }

    /**
     * 恢复出厂设置
     */
    async factoryReset() {
        return this.request('POST', '/system/factory-reset');
    }

    /**
     * 检查更新
     */
    async checkUpdate() {
        return this.request('GET', '/system/update/check');
    }

    // ==================== 网络 API ====================

    /**
     * 获取网络配置
     */
    async getNetworkConfig() {
        return this.request('GET', '/network/config');
    }

    /**
     * 更新网络配置
     */
    async updateNetworkConfig(config) {
        return this.request('PUT', '/network/config', config);
    }

    /**
     * 扫描 WiFi 网络
     */
    async scanWiFi() {
        return this.request('GET', '/network/scan');
    }

    /**
     * 连接 WiFi
     */
    async connectWiFi(ssid, password) {
        return this.request('POST', '/network/connect', { ssid, password });
    }

    /**
     * 断开 WiFi
     */
    async disconnectWiFi() {
        return this.request('POST', '/network/disconnect');
    }

    // ==================== 文件管理 API ====================

    /**
     * 获取文件列表
     */
    async getFileList(path = '/') {
        return this.request('GET', `/files?path=${encodeURIComponent(path)}`);
    }

    /**
     * 上传文件
     */
    async uploadFile(path, file) {
        const url = this.buildUrl(`/files?path=${encodeURIComponent(path)}`);
        const formData = new FormData();
        formData.append('file', file);

        const response = await fetch(url, {
            method: 'POST',
            body: formData
        });

        if (!response.ok) {
            const error = await response.json().catch(() => ({
                error: `HTTP ${response.status}`
            }));
            throw new Error(error.error || `HTTP ${response.status}`);
        }

        return response.json();
    }

    /**
     * 删除文件
     */
    async deleteFile(path) {
        return this.request('DELETE', `/files?path=${encodeURIComponent(path)}`);
    }

    // ==================== 批量操作 ====================

    /**
     * 批量获取状态（用于初始化）
     */
    async getInitialState() {
        const [config, radarStatus, systemInfo] = await Promise.all([
            this.getConfig().catch(() => null),
            this.getRadarStatus().catch(() => null),
            this.getSystemInfo().catch(() => null)
        ]);

        return {
            config,
            radarStatus,
            systemInfo,
            timestamp: Date.now()
        };
    }
}

// ==================== 便捷函数 ====================

/**
 * 创建 API 客户端实例
 */
function createApiClient(baseUrl) {
    return new ApiClient(baseUrl);
}

/**
 * 格式化字节大小
 */
function formatBytes(bytes) {
    if (bytes === 0) return '0 B';
    const k = 1024;
    const sizes = ['B', 'KB', 'MB', 'GB'];
    const i = Math.floor(Math.log(bytes) / Math.log(k));
    return parseFloat((bytes / Math.pow(k, i)).toFixed(2)) + ' ' + sizes[i];
}

/**
 * 格式化运行时间
 */
function formatUptime(seconds) {
    const days = Math.floor(seconds / 86400);
    const hours = Math.floor((seconds % 86400) / 3600);
    const minutes = Math.floor((seconds % 3600) / 60);
    const secs = Math.floor(seconds % 60);

    if (days > 0) {
        return `${days}天 ${hours}小时 ${minutes}分`;
    } else if (hours > 0) {
        return `${hours}小时 ${minutes}分 ${secs}秒`;
    } else {
        return `${minutes}分 ${secs}秒`;
    }
}

// 导出
if (typeof module !== 'undefined' && module.exports) {
    module.exports = { ApiClient, createApiClient, formatBytes, formatUptime };
}
