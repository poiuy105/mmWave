/**
 * RadarCanvas - 雷达可视化画布
 * 支持侧装(side)和顶装(ceiling)两种模式
 * 坐标系：以雷达为原点，Y轴正方向为正前方，X轴正方向为右侧
 */

class RadarCanvas {
    constructor(canvasId, options = {}) {
        this.canvas = document.getElementById(canvasId);
        this.ctx = this.canvas.getContext('2d');

        // 显示配置
        this.mountMode = options.mountMode || 'side'; // side | ceiling
        this.gridSize = options.gridSize || 0.5;       // 米
        this.showGrid = options.showGrid !== false;
        this.showTrail = options.showTrail !== false;
        this.trailLength = options.trailLength || 60;

        // 房间尺寸（米）
        this.roomWidth = options.roomWidth || 6.0;
        this.roomDepth = options.roomDepth || 8.0;

        // 视图变换
        this.scale = 60; // 像素/米
        this.offsetX = 0;
        this.offsetY = 0;

        // 交互状态
        this.isDragging = false;
        this.lastMouseX = 0;
        this.lastMouseY = 0;
        this.selectedTargetId = null;

        // 目标颜色（按 ID）
        this.targetColors = [
            '#ff6b6b', '#51cf66', '#339af0', '#fcc419',
            '#cc5de8', '#ff922b', '#20c997', '#f06595'
        ];

        // 当前目标数据（由外部更新）
        this.targets = [];

        // 动画
        this.animFrameId = null;
        this._dpr = window.devicePixelRatio || 1;

        // 性能监控
        this._perf = {
            frameCount: 0,
            lastFpsTime: performance.now(),
            fps: 0,
            frameTime: 0,
            lastFrameTime: 0,
            renderTime: 0
        };
        this.onPerformanceUpdate = null; // (perf) => void

        this._init();
    }

    /**
     * 初始化
     */
    _init() {
        this._resize();
        this._setupEvents();
        this._centerView();
    }

    /**
     * 调整画布大小（适配高 DPI）
     */
    _resize() {
        const container = this.canvas.parentElement;
        const rect = container.getBoundingClientRect();
        const w = rect.width;
        const h = rect.height;

        this.canvas.width = w * this._dpr;
        this.canvas.height = h * this._dpr;
        this.canvas.style.width = w + 'px';
        this.canvas.style.height = h + 'px';

        this.ctx.setTransform(this._dpr, 0, 0, this._dpr, 0, 0);

        this._displayWidth = w;
        this._displayHeight = h;
    }

    /**
     * 居中视图
     */
    _centerView() {
        // 计算合适的缩放比例，使房间适配画布
        const padding = 60;
        const availW = this._displayWidth - padding * 2;
        const availH = this._displayHeight - padding * 2;

        let scaleX, scaleY;
        if (this.mountMode === 'side') {
            scaleX = availW / this.roomWidth;
            scaleY = availH / this.roomDepth;
        } else {
            scaleX = availW / this.roomWidth;
            scaleY = availH / this.roomWidth; // 顶装模式正方形
        }

        this.scale = Math.min(scaleX, scaleY);
        this.scale = Math.max(10, Math.min(200, this.scale)); // 限制范围

        // 居中偏移 - 根据安装模式调整
        if (this.mountMode === 'side') {
            // 侧装：雷达在底部中央，扇形向上展开
            // 让扇形整体居中，下方留出较少空间
            this.offsetX = this._displayWidth / 2;
            this.offsetY = this._displayHeight - padding; // 雷达位置靠近底部
        } else {
            // 顶装：雷达在中央，圆形向四周展开
            this.offsetX = this._displayWidth / 2;
            this.offsetY = this._displayHeight / 2;
        }
    }

    /**
     * 设置事件监听
     */
    _setupEvents() {
        // 存储所有事件处理器以便清理
        this._eventHandlers = [];

        const addListener = (element, type, handler, options) => {
            element.addEventListener(type, handler, options);
            this._eventHandlers.push({ element, type, handler, options });
        };

        // 窗口大小变化
        this._resizeHandler = () => {
            this._resize();
            this._centerView();
        };
        addListener(window, 'resize', this._resizeHandler);

        // 鼠标滚轮缩放
        this._wheelHandler = (e) => {
            e.preventDefault();
            const zoomFactor = e.deltaY < 0 ? 1.1 : 0.9;
            this.scale *= zoomFactor;
            this.scale = Math.max(10, Math.min(200, this.scale));
        };
        addListener(this.canvas, 'wheel', this._wheelHandler, { passive: false });

        // 鼠标拖拽平移
        this._mouseDownHandler = (e) => {
            this.isDragging = true;
            this.lastMouseX = e.clientX;
            this.lastMouseY = e.clientY;
        };
        addListener(this.canvas, 'mousedown', this._mouseDownHandler);

        this._mouseMoveHandler = (e) => {
            if (this.isDragging) {
                this.offsetX += e.clientX - this.lastMouseX;
                this.offsetY += e.clientY - this.lastMouseY;
                this.lastMouseX = e.clientX;
                this.lastMouseY = e.clientY;
            }
        };
        addListener(window, 'mousemove', this._mouseMoveHandler);

        this._mouseUpHandler = () => {
            this.isDragging = false;
        };
        addListener(window, 'mouseup', this._mouseUpHandler);

        // 触摸支持
        this._touchState = {
            touchStartDist: 0,
            touchStartScale: 0,
            lastTouchX: 0,
            lastTouchY: 0
        };

        this._touchStartHandler = (e) => {
            if (e.touches.length === 1) {
                this.isDragging = true;
                this._touchState.lastTouchX = e.touches[0].clientX;
                this._touchState.lastTouchY = e.touches[0].clientY;
            } else if (e.touches.length === 2) {
                this.isDragging = false;
                const dx = e.touches[0].clientX - e.touches[1].clientX;
                const dy = e.touches[0].clientY - e.touches[1].clientY;
                this._touchState.touchStartDist = Math.sqrt(dx * dx + dy * dy);
                this._touchState.touchStartScale = this.scale;
            }
        };
        addListener(this.canvas, 'touchstart', this._touchStartHandler, { passive: true });

        this._touchMoveHandler = (e) => {
            e.preventDefault();
            if (e.touches.length === 1 && this.isDragging) {
                this.offsetX += e.touches[0].clientX - this._touchState.lastTouchX;
                this.offsetY += e.touches[0].clientY - this._touchState.lastTouchY;
                this._touchState.lastTouchX = e.touches[0].clientX;
                this._touchState.lastTouchY = e.touches[0].clientY;
            } else if (e.touches.length === 2) {
                const dx = e.touches[0].clientX - e.touches[1].clientX;
                const dy = e.touches[0].clientY - e.touches[1].clientY;
                const dist = Math.sqrt(dx * dx + dy * dy);
                this.scale = this._touchState.touchStartScale * (dist / this._touchState.touchStartDist);
                this.scale = Math.max(10, Math.min(200, this.scale));
            }
        };
        addListener(this.canvas, 'touchmove', this._touchMoveHandler, { passive: false });

        this._touchEndHandler = () => {
            this.isDragging = false;
        };
        addListener(this.canvas, 'touchend', this._touchEndHandler);

        // 鼠标位置显示
        this._mouseMoveInfoHandler = (e) => {
            if (this.isDragging) return;
            const rect = this.canvas.getBoundingClientRect();
            const sx = e.clientX - rect.left;
            const sy = e.clientY - rect.top;
            const world = this.screenToWorld(sx, sy);
            if (this.onMouseMove) {
                this.onMouseMove(world);
            }
        };
        addListener(this.canvas, 'mousemove', this._mouseMoveInfoHandler);
    }

    /**
     * 世界坐标 → 屏幕坐标
     * 侧装模式：X轴水平，Y轴（前方）朝上
     * 顶装模式：X轴水平，Y轴（前方）朝上
     */
    worldToScreen(wx, wy) {
        if (this.mountMode === 'side') {
            return {
                x: this.offsetX + wx * this.scale,
                y: this.offsetY - wy * this.scale
            };
        } else {
            // 顶装模式：雷达在中心
            return {
                x: this.offsetX + wx * this.scale,
                y: this.offsetY - wy * this.scale
            };
        }
    }

    /**
     * 屏幕坐标 → 世界坐标
     */
    screenToWorld(sx, sy) {
        return {
            x: (sx - this.offsetX) / this.scale,
            y: -(sy - this.offsetY) / this.scale
        };
    }

    /**
     * 更新目标数据
     */
    setTargets(targets) {
        this.targets = targets;
    }

    /**
     * 设置安装模式
     */
    setMountMode(mode) {
        this.mountMode = mode;
        this._centerView();
    }

    /**
     * 主渲染循环
     */
    start() {
        const loop = () => {
            const frameStart = performance.now();

            this._render();

            const frameEnd = performance.now();
            const renderTime = frameEnd - frameStart;

            // 更新性能统计
            this._perf.frameCount++;
            this._perf.renderTime = renderTime;
            this._perf.frameTime = frameStart - this._perf.lastFrameTime;
            this._perf.lastFrameTime = frameStart;

            // 每秒计算一次 FPS
            if (frameEnd - this._perf.lastFpsTime >= 1000) {
                this._perf.fps = this._perf.frameCount;
                this._perf.frameCount = 0;
                this._perf.lastFpsTime = frameEnd;

                // 触发性能更新回调
                if (this.onPerformanceUpdate) {
                    this.onPerformanceUpdate({
                        fps: this._perf.fps,
                        frameTime: Math.round(this._perf.frameTime * 10) / 10,
                        renderTime: Math.round(this._perf.renderTime * 10) / 10
                    });
                }

                // 性能警告
                if (this._perf.fps < 30) {
                    console.warn(`[Canvas] 性能警告: FPS=${this._perf.fps}, 渲染时间=${this._perf.renderTime.toFixed(1)}ms`);
                }
            }

            this.animFrameId = requestAnimationFrame(loop);
        };
        loop();
    }

    /**
     * 停止渲染
     */
    stop() {
        if (this.animFrameId) {
            cancelAnimationFrame(this.animFrameId);
            this.animFrameId = null;
        }
    }

    /**
     * 渲染一帧
     */
    _render() {
        try {
            const ctx = this.ctx;
            const w = this._displayWidth;
            const h = this._displayHeight;

            // 清空
            ctx.fillStyle = '#0d1117';
            ctx.fillRect(0, 0, w, h);

            // 绘制网格
            if (this.showGrid) {
                this._drawGrid(ctx);
            }

            // 绘制雷达探测范围
            this._drawRadarRange(ctx);

            // 绘制坐标轴
            this._drawAxes(ctx);

            // 绘制轨迹
            if (this.showTrail) {
                this._drawTrails(ctx);
            }

            // 绘制目标
            this._drawTargets(ctx);

            // 绘制雷达位置
            this._drawRadar(ctx);
        } catch (e) {
            console.error('[Canvas] Render error:', e);
            // 尝试恢复：清空画布并显示错误
            try {
                this.ctx.fillStyle = '#0d1117';
                this.ctx.fillRect(0, 0, this._displayWidth, this._displayHeight);
                this.ctx.fillStyle = '#ff4444';
                this.ctx.font = '14px sans-serif';
                this.ctx.fillText('渲染错误，请刷新页面', 10, 30);
            } catch (e2) {
                // 二次失败，无法恢复
            }
        }
    }

    /**
     * 绘制网格
     */
    _drawGrid(ctx) {
        const gs = this.gridSize;
        const w = this._displayWidth;
        const h = this._displayHeight;

        // 计算可见范围（世界坐标）
        const topLeft = this.screenToWorld(0, 0);
        const bottomRight = this.screenToWorld(w, h);

        const minX = Math.floor(topLeft.x / gs) * gs;
        const maxX = Math.ceil(bottomRight.x / gs) * gs;
        const minY = Math.floor(bottomRight.y / gs) * gs;
        const maxY = Math.ceil(topLeft.y / gs) * gs;

        ctx.lineWidth = 1;

        // 竖线
        for (let x = minX; x <= maxX; x += gs) {
            const sx = this.worldToScreen(x, 0).x;
            const isMajor = Math.abs(x % 1) < 0.01 || Math.abs(x) < 0.01;
            ctx.strokeStyle = isMajor ? 'rgba(48, 54, 61, 0.8)' : 'rgba(48, 54, 61, 0.3)';
            ctx.beginPath();
            ctx.moveTo(sx, 0);
            ctx.lineTo(sx, h);
            ctx.stroke();
        }

        // 横线
        for (let y = minY; y <= maxY; y += gs) {
            const sy = this.worldToScreen(0, y).y;
            const isMajor = Math.abs(y % 1) < 0.01 || Math.abs(y) < 0.01;
            ctx.strokeStyle = isMajor ? 'rgba(48, 54, 61, 0.8)' : 'rgba(48, 54, 61, 0.3)';
            ctx.beginPath();
            ctx.moveTo(0, sy);
            ctx.lineTo(w, sy);
            ctx.stroke();
        }

        // 网格标签（每米标注）
        ctx.font = '10px "SF Mono", Consolas, monospace';
        ctx.fillStyle = 'rgba(139, 148, 158, 0.5)';
        ctx.textAlign = 'center';
        ctx.textBaseline = 'top';

        const origin = this.worldToScreen(0, 0);

        // X 轴标签
        for (let x = Math.ceil(minX); x <= Math.floor(maxX); x += 1) {
            if (x === 0) continue;
            const sx = this.worldToScreen(x, 0).x;
            ctx.fillText(x + 'm', sx, origin.y + 4);
        }

        // Y 轴标签
        ctx.textAlign = 'right';
        ctx.textBaseline = 'middle';
        for (let y = Math.ceil(minY); y <= Math.floor(maxY); y += 1) {
            if (y === 0) continue;
            const sy = this.worldToScreen(0, y).y;
            ctx.fillText(y + 'm', origin.x - 6, sy);
        }
    }

    /**
     * 绘制坐标轴
     */
    _drawAxes(ctx) {
        const origin = this.worldToScreen(0, 0);
        const w = this._displayWidth;
        const h = this._displayHeight;

        ctx.strokeStyle = 'rgba(139, 148, 158, 0.3)';
        ctx.lineWidth = 1.5;

        // X 轴
        ctx.beginPath();
        ctx.moveTo(0, origin.y);
        ctx.lineTo(w, origin.y);
        ctx.stroke();

        // Y 轴
        ctx.beginPath();
        ctx.moveTo(origin.x, 0);
        ctx.lineTo(origin.x, h);
        ctx.stroke();

        // 轴标签
        ctx.font = '11px "SF Mono", Consolas, monospace';
        ctx.fillStyle = 'rgba(139, 148, 158, 0.6)';

        // X 轴标签
        ctx.textAlign = 'right';
        ctx.textBaseline = 'top';
        ctx.fillText('X →', w - 8, origin.y + 6);

        // Y 轴标签
        ctx.textAlign = 'left';
        ctx.textBaseline = 'bottom';
        ctx.fillText('Y ↑', origin.x + 8, 16);
    }

    /**
     * 绘制雷达探测范围
     */
    _drawRadarRange(ctx) {
        const origin = this.worldToScreen(0, 0);
        const range = this.roomDepth; // 使用房间深度作为探测范围

        if (this.mountMode === 'side') {
            // 侧装模式：扇形区域
            ctx.fillStyle = 'rgba(63, 185, 80, 0.05)';
            ctx.strokeStyle = 'rgba(63, 185, 80, 0.2)';
            ctx.lineWidth = 1;

            const radius = range * this.scale;
            ctx.beginPath();
            ctx.moveTo(origin.x, origin.y);
            ctx.arc(origin.x, origin.y, radius, -Math.PI / 2 - Math.PI / 3, -Math.PI / 2 + Math.PI / 3);
            ctx.closePath();
            ctx.fill();
            ctx.stroke();
        } else {
            // 顶装模式：圆形区域
            ctx.fillStyle = 'rgba(63, 185, 80, 0.05)';
            ctx.strokeStyle = 'rgba(63, 185, 80, 0.2)';
            ctx.lineWidth = 1;

            const radius = range * this.scale;
            ctx.beginPath();
            ctx.arc(origin.x, origin.y, radius, 0, Math.PI * 2);
            ctx.fill();
            ctx.stroke();

            // 距离圈（每 2 米）
            ctx.strokeStyle = 'rgba(63, 185, 80, 0.1)';
            ctx.setLineDash([4, 4]);
            for (let r = 2; r <= range; r += 2) {
                ctx.beginPath();
                ctx.arc(origin.x, origin.y, r * this.scale, 0, Math.PI * 2);
                ctx.stroke();
            }
            ctx.setLineDash([]);
        }
    }

    /**
     * 绘制轨迹
     */
    _drawTrails(ctx) {
        for (const target of this.targets) {
            if (!target.trail || target.trail.length < 2) continue;

            const color = this._getTargetColor(target.id);
            const trail = target.trail;

            ctx.lineWidth = 2;
            ctx.lineCap = 'round';
            ctx.lineJoin = 'round';

            for (let i = 1; i < trail.length; i++) {
                const alpha = (i / trail.length) * 0.6;
                ctx.strokeStyle = this._colorWithAlpha(color, alpha);

                const p0 = this.worldToScreen(trail[i - 1].x, trail[i - 1].y);
                const p1 = this.worldToScreen(trail[i].x, trail[i].y);

                ctx.beginPath();
                ctx.moveTo(p0.x, p0.y);
                ctx.lineTo(p1.x, p1.y);
                ctx.stroke();
            }
        }
    }

    /**
     * 绘制目标
     */
    _drawTargets(ctx) {
        for (const target of this.targets) {
            const pos = this.worldToScreen(target.x, target.y);
            const color = this._getTargetColor(target.id);
            const isSelected = target.id === this.selectedTargetId;
            const radius = isSelected ? 10 : 7;

            // 目标光晕
            const gradient = ctx.createRadialGradient(pos.x, pos.y, 0, pos.x, pos.y, radius * 2.5);
            gradient.addColorStop(0, this._colorWithAlpha(color, 0.3));
            gradient.addColorStop(1, this._colorWithAlpha(color, 0));
            ctx.fillStyle = gradient;
            ctx.beginPath();
            ctx.arc(pos.x, pos.y, radius * 2.5, 0, Math.PI * 2);
            ctx.fill();

            // 目标圆圈
            ctx.fillStyle = this._colorWithAlpha(color, 0.9);
            ctx.beginPath();
            ctx.arc(pos.x, pos.y, radius, 0, Math.PI * 2);
            ctx.fill();

            // 选中边框
            if (isSelected) {
                ctx.strokeStyle = '#ffffff';
                ctx.lineWidth = 2;
                ctx.beginPath();
                ctx.arc(pos.x, pos.y, radius + 3, 0, Math.PI * 2);
                ctx.stroke();
            }

            // ID 标签
            ctx.font = 'bold 11px "SF Mono", Consolas, monospace';
            ctx.fillStyle = '#ffffff';
            ctx.textAlign = 'left';
            ctx.textBaseline = 'middle';
            ctx.fillText(`#${target.id}`, pos.x + radius + 6, pos.y - 2);

            // 速度标签
            if (target.speed > 0.01) {
                ctx.font = '10px "SF Mono", Consolas, monospace';
                ctx.fillStyle = 'rgba(139, 148, 158, 0.8)';
                ctx.fillText(`${target.speed.toFixed(1)} m/s`, pos.x + radius + 6, pos.y + 10);
            }

            // 选中时显示详细信息
            if (isSelected) {
                this._drawTargetInfo(ctx, target, pos);
            }
        }
    }

    /**
     * 绘制选中目标的详细信息
     */
    _drawTargetInfo(ctx, target, pos) {
        const info = [
            `ID: ${target.id}`,
            `X: ${target.x.toFixed(2)} m`,
            `Y: ${target.y.toFixed(2)} m`,
            `Z: ${(target.z || 0).toFixed(2)} m`,
            `Speed: ${target.speed.toFixed(2)} m/s`,
            `SNR: ${(target.snr || 0).toFixed(1)} dB`,
            `Conf: ${((target.confidence || 0) * 100).toFixed(0)}%`
        ];

        const lineHeight = 16;
        const padding = 10;
        const boxW = 140;
        const boxH = info.length * lineHeight + padding * 2;

        let boxX = pos.x + 20;
        let boxY = pos.y - boxH / 2;

        // 防止超出画布
        if (boxX + boxW > this._displayWidth) boxX = pos.x - boxW - 20;
        if (boxY < 0) boxY = 4;
        if (boxY + boxH > this._displayHeight) boxY = this._displayHeight - boxH - 4;

        // 背景
        ctx.fillStyle = 'rgba(13, 17, 23, 0.9)';
        ctx.strokeStyle = 'rgba(48, 54, 61, 0.8)';
        ctx.lineWidth = 1;
        this._roundRect(ctx, boxX, boxY, boxW, boxH, 6);
        ctx.fill();
        ctx.stroke();

        // 文字
        ctx.font = '11px "SF Mono", Consolas, monospace';
        ctx.textAlign = 'left';
        ctx.textBaseline = 'top';

        for (let i = 0; i < info.length; i++) {
            ctx.fillStyle = i === 0 ? '#58a6ff' : 'rgba(230, 237, 243, 0.8)';
            ctx.fillText(info[i], boxX + padding, boxY + padding + i * lineHeight);
        }
    }

    /**
     * 绘制雷达位置标记
     */
    _drawRadar(ctx) {
        const origin = this.worldToScreen(0, 0);

        // 雷达三角形图标
        const size = 12;
        ctx.fillStyle = '#3fb950';
        ctx.beginPath();
        ctx.moveTo(origin.x, origin.y - size);
        ctx.lineTo(origin.x - size * 0.7, origin.y + size * 0.5);
        ctx.lineTo(origin.x + size * 0.7, origin.y + size * 0.5);
        ctx.closePath();
        ctx.fill();

        // 外圈光晕
        ctx.strokeStyle = 'rgba(63, 185, 80, 0.3)';
        ctx.lineWidth = 1.5;
        ctx.beginPath();
        ctx.arc(origin.x, origin.y, size + 4, 0, Math.PI * 2);
        ctx.stroke();
    }

    /**
     * 获取目标颜色
     */
    _getTargetColor(id) {
        return this.targetColors[(id - 1) % this.targetColors.length];
    }

    /**
     * 颜色加透明度
     */
    _colorWithAlpha(hex, alpha) {
        const r = parseInt(hex.slice(1, 3), 16);
        const g = parseInt(hex.slice(3, 5), 16);
        const b = parseInt(hex.slice(5, 7), 16);
        return `rgba(${r},${g},${b},${alpha})`;
    }

    /**
     * 圆角矩形
     */
    _roundRect(ctx, x, y, w, h, r) {
        ctx.beginPath();
        ctx.moveTo(x + r, y);
        ctx.lineTo(x + w - r, y);
        ctx.quadraticCurveTo(x + w, y, x + w, y + r);
        ctx.lineTo(x + w, y + h - r);
        ctx.quadraticCurveTo(x + w, y + h, x + w - r, y + h);
        ctx.lineTo(x + r, y + h);
        ctx.quadraticCurveTo(x, y + h, x, y + h - r);
        ctx.lineTo(x, y + r);
        ctx.quadraticCurveTo(x, y, x + r, y);
        ctx.closePath();
    }

    /**
     * 销毁
     */
    destroy() {
        this.stop();

        // 清理所有事件监听器
        if (this._eventHandlers) {
            for (const { element, type, handler, options } of this._eventHandlers) {
                element.removeEventListener(type, handler, options);
            }
            this._eventHandlers = [];
        }

        // 清理触摸状态
        if (this._touchState) {
            this._touchState = null;
        }

        // 清理画布引用
        this.ctx = null;
        this.canvas = null;
    }
}
