/**
 * LD Radar Monitor - 应用主入口
 */

// 全局应用实例
const App = {
    // 组件引用
    canvas: null,
    ws: null,
    
    // 状态
    isConnected: false,
    startTime: Date.now(),
    frameCount: 0,
    lastFpsUpdate: Date.now(),
    currentFps: 0,
    
    /**
     * 初始化应用
     */
    init() {
        Logger.info('Initializing LD Radar Monitor...');
        
        // 初始化画布
        this.initCanvas();
        
        // 初始化事件监听
        this.initEventListeners();
        
        // 初始化 UI
        this.initUI();
        
        // 启动运行时间计时器
        this.startUptimeTimer();
        
        // 启动帧率计算
        this.startFpsCounter();
        
        Logger.info('Application initialized');
    },
    
    /**
     * 初始化画布
     */
    initCanvas() {
        const canvas = document.getElementById('radarCanvas');
        if (!canvas) {
            Logger.error('Canvas element not found');
            return;
        }
        
        // 调整画布大小以适应容器
        this.resizeCanvas();
        window.addEventListener('resize', () => this.resizeCanvas());
        
        // 创建画布实例
        this.canvas = new RadarCanvas('radarCanvas', {
            mountMode: config.get('radar.mount_mode', 'side'),
            roomWidth: config.get('radar.room.width', 6),
            roomDepth: config.get('radar.room.depth', 8),
            gridSize: config.get('display.grid_size', 0.5),
            trailLength: config.get('display.trail_length', 60)
        });
        
        // 启动渲染
        this.canvas.start();
        
        // 模拟数据（测试用）
        this.simulateData();
    },
    
    /**
     * 调整画布大小
     */
    resizeCanvas() {
        const container = document.querySelector('.canvas-container');
        const canvas = document.getElementById('radarCanvas');
        if (!container || !canvas) return;
        
        const rect = container.getBoundingClientRect();
        const padding = 40;
        
        // 保持 4:3 比例
        let width = rect.width - padding * 2;
        let height = width * 0.75;
        
        if (height > rect.height - padding * 2) {
            height = rect.height - padding * 2;
            width = height / 0.75;
        }
        
        canvas.width = width;
        canvas.height = height;
        canvas.style.width = width + 'px';
        canvas.style.height = height + 'px';
    },
    
    /**
     * 初始化事件监听
     */
    initEventListeners() {
        // 安装模式切换
        const mountMode = document.getElementById('mountMode');
        if (mountMode) {
            mountMode.addEventListener('change', (e) => {
                const mode = e.target.value;
                config.set('radar.mount_mode', mode);
                if (this.canvas) {
                    this.canvas.config.mountMode = mode;
                }
                this.updateInstallModeDisplay(mode);
            });
        }
        
        // 设置按钮
        const btnSettings = document.getElementById('btnSettings');
        if (btnSettings) {
            btnSettings.addEventListener('click', () => this.openSettings());
        }
        
        // 关闭设置
        const btnCloseSettings = document.getElementById('btnCloseSettings');
        if (btnCloseSettings) {
            btnCloseSettings.addEventListener('click', () => this.closeSettings());
        }
        
        // 保存设置
        const btnSaveSettings = document.getElementById('btnSaveSettings');
        if (btnSaveSettings) {
            btnSaveSettings.addEventListener('click', () => this.saveSettings());
        }
        
        // 取消设置
        const btnCancelSettings = document.getElementById('btnCancelSettings');
        if (btnCancelSettings) {
            btnCancelSettings.addEventListener('click', () => this.closeSettings());
        }
        
        // 日志按钮
        const btnLogs = document.getElementById('btnLogs');
        if (btnLogs) {
            btnLogs.addEventListener('click', () => this.openLogs());
        }
        
        // 关闭日志
        const btnCloseLogs = document.getElementById('btnCloseLogs');
        if (btnCloseLogs) {
            btnCloseLogs.addEventListener('click', () => this.closeLogs());
        }
        
        // 设置标签页切换
        document.querySelectorAll('.tab-btn').forEach(btn => {
            btn.addEventListener('click', (e) => this.switchTab(e.target.dataset.tab));
        });
        
        // 配置变更监听
        config.subscribe((path, newValue) => {
            Logger.info(`Config changed: ${path} =`, newValue);
        });
    },
    
    /**
     * 初始化 UI
     */
    initUI() {
        // 加载保存的配置到 UI
        const mountMode = document.getElementById('mountMode');
        if (mountMode) {
            mountMode.value = config.get('radar.mount_mode', 'side');
        }
        
        // 更新显示
        this.updateSystemInfo();
    },
    
    /**
     * 更新系统信息显示
     */
    updateSystemInfo() {
        const radarType = document.getElementById('radarType');
        const baudRate = document.getElementById('baudRate');
        const installMode = document.getElementById('installMode');
        
        if (radarType) radarType.textContent = config.get('radar.type', '-');
        if (baudRate) baudRate.textContent = config.get('radar.baud_rate', '-');
        if (installMode) installMode.textContent = config.get('radar.mount_mode', 'side') === 'side' ? '侧装' : '顶装';
    },
    
    /**
     * 更新安装模式显示
     */
    updateInstallModeDisplay(mode) {
        const installMode = document.getElementById('installMode');
        if (installMode) {
            installMode.textContent = mode === 'side' ? '侧装' : '顶装';
        }
    },
    
    /**
     * 打开设置面板
     */
    openSettings() {
        const panel = document.getElementById('settingsPanel');
        if (panel) {
            panel.classList.remove('hidden');
            this.loadSettingsToForm();
        }
    },
    
    /**
     * 关闭设置面板
     */
    closeSettings() {
        const panel = document.getElementById('settingsPanel');
        if (panel) {
            panel.classList.add('hidden');
        }
    },
    
    /**
     * 加载设置到表单
     */
    loadSettingsToForm() {
        // 雷达类型
        const radarType = document.getElementById('settingRadarType');
        if (radarType) radarType.value = config.get('radar.type', 'LD2460');
        
        // 波特率
        const baudRate = document.getElementById('settingBaudRate');
        if (baudRate) baudRate.value = config.get('radar.baud_rate', 115200);
        
        // 网格大小
        const gridSize = document.getElementById('settingGridSize');
        if (gridSize) gridSize.value = config.get('display.grid_size', 0.5);
        
        // 轨迹长度
        const trailLength = document.getElementById('settingTrailLength');
        if (trailLength) trailLength.value = config.get('display.trail_length', 60);
        
        // 显示选项
        const showGrid = document.getElementById('settingShowGrid');
        if (showGrid) showGrid.checked = config.get('display.show_grid', true);
        
        const showTrail = document.getElementById('settingShowTrail');
        if (showTrail) showTrail.checked = config.get('display.show_trail', true);
    },
    
    /**
     * 保存设置
     */
    saveSettings() {
        // 雷达类型
        const radarType = document.getElementById('settingRadarType');
        if (radarType) config.set('radar.type', radarType.value);
        
        // 波特率
        const baudRate = document.getElementById('settingBaudRate');
        if (baudRate) config.set('radar.baud_rate', parseInt(baudRate.value));
        
        // 网格大小
        const gridSize = document.getElementById('settingGridSize');
        if (gridSize) config.set('display.grid_size', parseFloat(gridSize.value));
        
        // 轨迹长度
        const trailLength = document.getElementById('settingTrailLength');
        if (trailLength) {
            const value = parseInt(trailLength.value);
            config.set('display.trail_length', value);
            if (this.canvas) {
                this.canvas.config.trailLength = value;
            }
        }
        
        // 显示选项
        const showGrid = document.getElementById('settingShowGrid');
        if (showGrid) config.set('display.show_grid', showGrid.checked);
        
        const showTrail = document.getElementById('settingShowTrail');
        if (showTrail) config.set('display.show_trail', showTrail.checked);
        
        // 更新 UI
        this.updateSystemInfo();
        
        // 关闭面板
        this.closeSettings();
        
        Logger.info('Settings saved');
    },
    
    /**
     * 打开日志面板
     */
    openLogs() {
        const panel = document.getElementById('logsPanel');
        if (panel) {
            panel.classList.remove('hidden');
        }
    },
    
    /**
     * 关闭日志面板
     */
    closeLogs() {
        const panel = document.getElementById('logsPanel');
        if (panel) {
            panel.classList.add('hidden');
        }
    },
    
    /**
     * 切换标签页
     */
    switchTab(tabName) {
        // 更新按钮状态
        document.querySelectorAll('.tab-btn').forEach(btn => {
            btn.classList.toggle('active', btn.dataset.tab === tabName);
        });
        
        // 更新内容显示
        document.querySelectorAll('.tab-pane').forEach(pane => {
            pane.classList.toggle('active', pane.id === `tab-${tabName}`);
        });
    },
    
    /**
     * 启动运行时间计时器
     */
    startUptimeTimer() {
        setInterval(() => {
            const uptime = Math.floor((Date.now() - this.startTime) / 1000);
            const uptimeEl = document.getElementById('uptime');
            if (uptimeEl) {
                uptimeEl.textContent = formatTime(uptime);
            }
        }, 1000);
    },
    
    /**
     * 启动帧率计数器
     */
    startFpsCounter() {
        setInterval(() => {
            const now = Date.now();
            const elapsed = (now - this.lastFpsUpdate) / 1000;
            this.currentFps = Math.round(this.frameCount / elapsed);
            this.frameCount = 0;
            this.lastFpsUpdate = now;
            
            const fpsEl = document.getElementById('frameRate');
            if (fpsEl) {
                fpsEl.textContent = `帧率: ${this.currentFps} fps`;
            }
        }, 2000);
    },
    
    /**
     * 更新帧计数
     */
    updateFrame() {
        this.frameCount++;
    },
    
    /**
     * 模拟数据（测试用）
     */
    simulateData() {
        // 模拟目标移动
        let angle = 0;
        const radius = 2;
        
        setInterval(() => {
            angle += 0.1;
            const x = Math.cos(angle) * radius;
            const y = Math.sin(angle) * radius + 3;
            
            if (this.canvas) {
                this.canvas.updateTargets([{
                    id: 0,
                    x: x,
                    y: y,
                    speed: 0.5
                }]);
            }
            
            // 更新目标列表显示
            this.updateTargetList([{id: 0, x, y, speed: 0.5}]);
            
            // 更新目标数
            const targetCount = document.getElementById('targetCount');
            if (targetCount) targetCount.textContent = '1';
            
        }, 100);
    },
    
    /**
     * 更新目标列表显示
     */
    updateTargetList(targets) {
        const list = document.getElementById('targetList');
        if (!list) return;
        
        if (targets.length === 0) {
            list.innerHTML = '<div class="target-item empty">等待数据...</div>';
            return;
        }
        
        list.innerHTML = targets.map(t => `
            <div class="target-item">
                <span class="target-id">#${t.id}</span>
                <span class="target-coords">x:${t.x.toFixed(2)} y:${t.y.toFixed(2)}</span>
            </div>
        `).join('');
    }
};

// DOM 加载完成后初始化
document.addEventListener('DOMContentLoaded', () => {
    App.init();
});
