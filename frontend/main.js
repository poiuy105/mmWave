/**
 * LD Radar Monitor - 应用主入口
 * 串联 WebSocket → RadarDataManager → RadarCanvas 数据流
 */

const App = {
    // 模块实例
    api: null,
    ws: null,
    radarData: null,
    canvas: null,
    zoneManager: null,
    zoneEditor: null,

    // 状态
    startTime: Date.now(),

    // DOM 引用
    els: {},

    // 定时器引用（用于清理）
    timers: {
        uptime: null,
        toasts: new Set()
    },

    /**
     * 初始化
     */
    init() {
        this._cacheElements();
        this._initModules();
        this._initEvents();
        this._connect();
        this._startTimers();
        console.log('[App] 初始化完成');
    },

    /**
     * 缓存 DOM 元素
     */
    _cacheElements() {
        const $ = (id) => document.getElementById(id);
        this.els = {
            wsStatus: $('wsStatus'),
            mountMode: $('mountMode'),
            targetList: $('targetList'),
            radarType: $('radarType'),
            targetCount: $('targetCount'),
            installMode: $('installMode'),
            uptime: $('uptime'),
            memory: $('memory'),
            frameCount: $('frameCount'),
            connectionStatus: $('connectionStatus'),
            frameRate: $('frameRate'),
            latency: $('latency'),
            coordinates: $('coordinates'),
            statTargets: $('statTargets'),
            statFps: $('statFps'),
            statLatency: $('statLatency'),
            radarDirection: $('radarDirection'),
            // 设置面板
            settingsPanel: $('settingsPanel'),
            btnSettings: $('btnSettings'),
            btnCloseSettings: $('btnCloseSettings'),
            btnSaveSettings: $('btnSaveSettings'),
            btnCancelSettings: $('btnCancelSettings'),
            settingMountMode: $('settingMountMode'),
            settingDistance: $('settingDistance'),
            settingHeight: $('settingHeight'),
            settingAngle: $('settingAngle'),
            settingGridSize: $('settingGridSize'),
            settingTrailLength: $('settingTrailLength'),
            settingShowGrid: $('settingShowGrid'),
            settingShowTrail: $('settingShowTrail'),
            toastContainer: $('toastContainer'),
            // 区域管理
            zoneList: $('zoneList'),
            btnAddZone: $('btnAddZone')
        };
    },

    /**
     * 初始化模块
     */
    _initModules() {
        // API 客户端
        this.api = new ApiClient();

        // 区域管理器
        this.zoneManager = new ZoneManager({
            maxZones: 8,
            maxPoints: 10
        });
        
        // 区域更新回调
        this.zoneManager.onZonesUpdate = (zones) => {
            this.canvas.setZones(zones);
            this._updateZoneList();
        };

        // 雷达数据管理
        this.radarData = new RadarDataManager({
            trailLength: 60
        });

        // 数据更新回调
        this.radarData.onTargetsUpdate = (targets) => {
            this.canvas.setTargets(targets);
            this._updateTargetList(targets);
        };

        this.radarData.onStatsUpdate = (stats) => {
            this._updateStats(stats);
        };

        // Canvas 渲染
        this.canvas = new RadarCanvas('radarCanvas', {
            mountMode: 'side',
            gridSize: 0.5,
            showGrid: true,
            showTrail: true,
            trailLength: 60,
            roomWidth: 6.0,
            roomDepth: 8.0
        });

        // Canvas 鼠标位置回调
        this.canvas.onMouseMove = (world) => {
            this.els.coordinates.textContent = `X: ${world.x.toFixed(2)} Y: ${world.y.toFixed(2)}`;
        };

        // 启动渲染循环
        this.canvas.start();
        
        // 区域编辑器
        this.zoneEditor = new ZoneEditor(this.canvas, this.zoneManager, this.api);
    },

    /**
     * 初始化事件监听
     */
    _initEvents() {
        // 安装模式切换（顶部快捷切换）
        this.els.mountMode.addEventListener('change', () => {
            const mode = this.els.mountMode.value;
            this._setMountMode(mode);
            // 同步到后端持久化
            this.api.setInstallMode(mode).catch(() => {});
        });

        // 设置面板
        this.els.btnSettings.addEventListener('click', () => this._openSettings());
        this.els.btnCloseSettings.addEventListener('click', () => this._closeSettings());
        this.els.btnCancelSettings.addEventListener('click', () => this._closeSettings());
        this.els.btnSaveSettings.addEventListener('click', () => this._saveSettings());

        // 点击模态框背景关闭
        this.els.settingsPanel.addEventListener('click', (e) => {
            if (e.target === this.els.settingsPanel) this._closeSettings();
        });

        // 标签页切换
        document.querySelectorAll('.tab-btn').forEach(btn => {
            btn.addEventListener('click', () => {
                document.querySelectorAll('.tab-btn').forEach(b => b.classList.remove('active'));
                document.querySelectorAll('.tab-pane').forEach(p => p.classList.remove('active'));
                btn.classList.add('active');
                document.getElementById('tab-' + btn.dataset.tab).classList.add('active');
            });
        });
        
        // 区域管理
        this.els.btnAddZone.addEventListener('click', () => {
            this.zoneEditor.startDrawing();
        });
    },

    /**
     * 连接 WebSocket
     */
    _connect() {
        this.ws = new WebSocketClient();

        // 注册消息处理器
        this.ws.onMessage('radar_data', (data) => {
            this.radarData.processRadarData(data);
        });
        
        // 区域配置消息
        this.ws.onMessage('config', (data) => {
            if (data.zones) {
                console.log('[App] 接收区域配置:', data.zones.length, '个');
                this.zoneManager.setZones(data.zones);
            }
        });
        
        // 实时数据消息（精简格式）
        this.ws.onMessage('data', (data) => {
            // 处理目标数据
            if (data.t && Array.isArray(data.t)) {
                const targets = this._parseCompactTargets(data.t);
                this.radarData.processRadarData({ targets });
            }
            
            // 处理区域触发状态
            if (data.z && Array.isArray(data.z)) {
                this.zoneManager.updateTriggerStates(data.z);
            }
        });

        // 连接状态事件
        this.ws.on('connected', () => {
            this._updateConnectionUI('connected');
            this._loadInitialState();
        });

        this.ws.on('disconnected', () => {
            this._updateConnectionUI('disconnected');
            // 清空过时的雷达目标数据，避免误导用户
            this.radarData.clearTargets();
        });

        this.ws.on('reconnecting', (info) => {
            this._updateConnectionUI('reconnecting');
        });

        this.ws.on('reconnect_failed', () => {
            this._showToast('连接失败，请刷新页面重试', 'error');
        });

        // 尝试连接
        this.ws.connect();
    },

    /**
     * 解析精简格式的目标数据
     * @param {array} compactTargets - [[id, x, y, speed], ...]
     * @returns {array} 标准格式目标数组
     */
    _parseCompactTargets(compactTargets) {
        return compactTargets.map(t => ({
            id: t[0],
            x: t[1],
            y: t[2],
            speed: t[3] || 0,
            z: 0,
            snr: 0,
            confidence: 0
        }));
    },

    /**
     * 加载初始状态
     */
    async _loadInitialState() {
        try {
            const state = await this.api.getInitialState();

            if (state.radarStatus) {
                const rs = state.radarStatus;
                this.els.radarType.textContent = rs.type || rs.name || '-';
                this.els.targetCount.textContent = rs.target_count || 0;

                if (rs.capabilities && rs.capabilities.has_install_mode) {
                    const mode = rs.capabilities.install_mode || 'side';
                    this.els.mountMode.value = mode === 'top' ? 'ceiling' : 'side';
                    this._setMountMode(mode === 'top' ? 'ceiling' : 'side');
                }
            }

            if (state.systemInfo) {
                const si = state.systemInfo;
                this.els.memory.textContent = formatBytes(si.free_heap || 0);
                this.els.frameCount.textContent = (si.radar_frames || 0).toLocaleString();
            }

            if (state.errors.length > 0) {
                console.warn('[App] 部分初始状态加载失败:', state.errors);
                this._showToast('部分数据加载失败，功能可能受限', 'warning');
            }
        } catch (e) {
            console.warn('[App] 加载初始状态失败:', e);
            this._showToast('数据加载失败，请检查网络连接', 'error');
        }
    },

    /**
     * 更新连接状态 UI
     */
    _updateConnectionUI(state) {
        const el = this.els.wsStatus;
        const statusEl = this.els.connectionStatus;

        el.className = 'ws-status ' + state;

        const labels = {
            connected: '已连接',
            disconnected: '未连接',
            reconnecting: '重连中...'
        };

        el.querySelector('.ws-text').textContent = labels[state] || state;

        statusEl.className = 'status-item';
        if (state === 'connected') {
            statusEl.classList.add('status-connected');
            statusEl.textContent = '● 已连接';
        } else if (state === 'reconnecting') {
            statusEl.style.color = '#d29922';
            statusEl.textContent = '● 重连中...';
        } else {
            statusEl.classList.add('status-disconnected');
            statusEl.textContent = '● 未连接';
        }
    },

    /**
     * 更新目标列表
     */
    _updateTargetList(targets) {
        const container = this.els.targetList;

        if (targets.length === 0) {
            container.innerHTML = '<div class="target-item empty">等待数据...</div>';
            return;
        }

        let html = '';
        for (const t of targets) {
            const color = this.canvas._getTargetColor(t.id);
            html += `
                <div class="target-item">
                    <span class="target-id" style="color:${color}">#${t.id}</span>
                    <span class="target-coords">(${t.x.toFixed(1)}, ${t.y.toFixed(1)})</span>
                    <span class="target-speed">${t.speed.toFixed(1)} m/s</span>
                </div>
            `;
        }
        container.innerHTML = html;
    },

    /**
     * 更新区域列表
     */
    _updateZoneList() {
        const container = this.els.zoneList;
        const zones = this.zoneManager.getZonesArray();
        
        if (zones.length === 0) {
            container.innerHTML = '<div class="zone-item empty">暂无区域</div>';
            return;
        }
        
        let html = '';
        for (const zone of zones) {
            const triggeredClass = zone.triggered ? ' triggered' : '';
            const statusText = zone.triggered ? '触发中' : '正常';
            const statusClass = zone.triggered ? ' triggered' : '';
            
            html += `
                <div class="zone-item${triggeredClass}">
                    <span class="zone-color" style="background:${zone.color}"></span>
                    <span class="zone-name">${zone.name}</span>
                    <span class="zone-status${statusClass}">${statusText}</span>
                </div>
            `;
        }
        container.innerHTML = html;
    },

    /**
     * 更新统计数据
     */
    _updateStats(stats) {
        this.els.statTargets.textContent = stats.targetCount;
        this.els.statFps.textContent = stats.fps + ' fps';
        this.els.statLatency.textContent = stats.latency + ' ms';
        this.els.frameRate.textContent = '帧率: ' + stats.fps + ' fps';
        this.els.latency.textContent = '延迟: ' + stats.latency + ' ms';
        this.els.targetCount.textContent = stats.targetCount;
    },

    /**
     * 设置安装模式
     */
    _setMountMode(mode) {
        this.canvas.setMountMode(mode);
        this.els.installMode.textContent = mode === 'side' ? '侧装' : '顶装';
        this.els.radarDirection.textContent = mode === 'side' ? '↑ 正前方' : '⊙ 俯视';

        // 同步设置面板
        this.els.settingMountMode.value = mode;
    },

    /**
     * 打开设置面板
     */
    _openSettings() {
        // 加载当前配置到表单
        this.els.settingMountMode.value = this.canvas.mountMode;
        this.els.settingDistance.value = this.canvas.roomDepth;
        this.els.settingGridSize.value = this.canvas.gridSize;
        this.els.settingTrailLength.value = this.canvas.trailLength;
        this.els.settingShowGrid.checked = this.canvas.showGrid;
        this.els.settingShowTrail.checked = this.canvas.showTrail;

        this.els.settingsPanel.classList.remove('hidden');
    },

    /**
     * 关闭设置面板
     */
    _closeSettings() {
        this.els.settingsPanel.classList.add('hidden');
    },

    /**
     * 保存设置
     */
    async _saveSettings() {
        const mountMode = this.els.settingMountMode.value;
        const gridSize = parseFloat(this.els.settingGridSize.value) || 0.5;
        const trailLength = parseInt(this.els.settingTrailLength.value) || 60;
        const showGrid = this.els.settingShowGrid.checked;
        const showTrail = this.els.settingShowTrail.checked;
        const distance = parseFloat(this.els.settingDistance.value) || 6.0;
        const height = parseFloat(this.els.settingHeight.value) || 2.5;
        const angle = parseFloat(this.els.settingAngle.value) || 0;

        // 更新 Canvas
        this.canvas.gridSize = gridSize;
        this.canvas.trailLength = trailLength;
        this.canvas.showGrid = showGrid;
        this.canvas.showTrail = showTrail;
        this.canvas.roomDepth = distance;
        this.radarData.setTrailLength(trailLength);

        // 更新安装模式
        if (mountMode !== this.canvas.mountMode) {
            this._setMountMode(mountMode);
            this.els.mountMode.value = mountMode;
        }

        // 调用 API 设置安装模式
        try {
            await this.api.setInstallMode(mountMode, height, angle);
            this._showToast('设置已保存', 'success');
        } catch (e) {
            this._showToast('设置保存失败: ' + e.message, 'error');
        }

        this._closeSettings();
    }

    /**
     * 保存区域配置
     */
    async _saveZones() {
        const zones = this.zoneManager.exportConfig();
        
        if (zones.length === 0) {
            this._showToast('至少需要一个区域', 'warning');
            return;
        }
        
        try {
            const result = await this.api.saveZones(zones);
            
            if (result.success) {
                this._showToast('区域配置已保存', 'success');
                console.log('[App] 区域配置保存成功');
            } else {
                this._showToast('保存失败: ' + (result.error || '未知错误'), 'error');
            }
        } catch (e) {
            console.error('[App] 保存区域异常:', e);
            this._showToast('网络错误，请检查连接', 'error');
        }
    },

    /**
     * 启动定时器（运行时间等）
     */
    _startTimers() {
        // 运行时间更新
        this.timers.uptime = setInterval(() => {
            const elapsed = Math.floor((Date.now() - this.startTime) / 1000);
            const h = Math.floor(elapsed / 3600).toString().padStart(2, '0');
            const m = Math.floor((elapsed % 3600) / 60).toString().padStart(2, '0');
            const s = (elapsed % 60).toString().padStart(2, '0');
            this.els.uptime.textContent = `${h}:${m}:${s}`;
        }, 1000);
    },

    /**
     * 清理所有定时器
     */
    _cleanupTimers() {
        // 清理运行时间定时器
        if (this.timers.uptime) {
            clearInterval(this.timers.uptime);
            this.timers.uptime = null;
        }

        // 清理所有 toast 定时器
        this.timers.toasts.forEach(timer => clearTimeout(timer));
        this.timers.toasts.clear();
    },

    /**
     * 显示 Toast 提示
     */
    _showToast(message, type = 'info', duration = 3000) {
        const container = this.els.toastContainer;
        const toast = document.createElement('div');
        toast.className = 'toast ' + type;
        toast.textContent = message;
        container.appendChild(toast);

        const timer1 = setTimeout(() => {
            toast.classList.add('toast-out');
            const timer2 = setTimeout(() => {
                toast.remove();
                this.timers.toasts.delete(timer1);
                this.timers.toasts.delete(timer2);
            }, 300);
            this.timers.toasts.add(timer2);
        }, duration);
        this.timers.toasts.add(timer1);
    },

    /**
     * 销毁应用
     */
    destroy() {
        console.log('[App] 正在销毁...');

        // 清理定时器
        this._cleanupTimers();

        // 断开 WebSocket
        if (this.ws) {
            this.ws.disconnect();
            this.ws = null;
        }

        // 销毁区域编辑器
        if (this.zoneEditor) {
            this.zoneEditor.destroy();
            this.zoneEditor = null;
        }

        // 销毁画布
        if (this.canvas) {
            this.canvas.destroy();
            this.canvas = null;
        }

        // 清理数据管理器
        if (this.radarData) {
            this.radarData.clear && this.radarData.clear();
            this.radarData = null;
        }
        
        // 清理区域管理器
        if (this.zoneManager) {
            this.zoneManager.clear();
            this.zoneManager = null;
        }

        console.log('[App] 销毁完成');
    }
};

// 启动应用
document.addEventListener('DOMContentLoaded', () => {
    App.init();
});
