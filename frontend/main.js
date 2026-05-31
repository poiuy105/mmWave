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

    // 状态
    startTime: Date.now(),

    // DOM 引用
    els: {},

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
            toastContainer: $('toastContainer')
        };
    },

    /**
     * 初始化模块
     */
    _initModules() {
        // API 客户端
        this.api = new ApiClient();

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
    },

    /**
     * 初始化事件监听
     */
    _initEvents() {
        // 安装模式切换（顶部快捷切换）
        this.els.mountMode.addEventListener('change', () => {
            const mode = this.els.mountMode.value;
            this._setMountMode(mode);
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

        // 连接状态事件
        this.ws.on('connected', () => {
            this._updateConnectionUI('connected');
            this._loadInitialState();
        });

        this.ws.on('disconnected', () => {
            this._updateConnectionUI('disconnected');
        });

        this.ws.on('reconnecting', (info) => {
            this._updateConnectionUI('reconnecting');
        });

        // 尝试连接
        this.ws.connect();
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
            }
        } catch (e) {
            console.warn('[App] 加载初始状态失败:', e);
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
            await this.api.setInstallMode(mountMode === 'ceiling' ? 'top' : 'side', height, angle);
            this._showToast('设置已保存', 'success');
        } catch (e) {
            this._showToast('设置保存失败: ' + e.message, 'error');
        }

        this._closeSettings();
    },

    /**
     * 启动定时器（运行时间等）
     */
    _startTimers() {
        // 运行时间更新
        setInterval(() => {
            const elapsed = Math.floor((Date.now() - this.startTime) / 1000);
            const h = Math.floor(elapsed / 3600).toString().padStart(2, '0');
            const m = Math.floor((elapsed % 3600) / 60).toString().padStart(2, '0');
            const s = (elapsed % 60).toString().padStart(2, '0');
            this.els.uptime.textContent = `${h}:${m}:${s}`;
        }, 1000);
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

        setTimeout(() => {
            toast.classList.add('toast-out');
            setTimeout(() => toast.remove(), 300);
        }, duration);
    }
};

// 启动应用
document.addEventListener('DOMContentLoaded', () => {
    App.init();
});
