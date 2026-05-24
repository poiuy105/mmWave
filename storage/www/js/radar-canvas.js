/**
 * RadarCanvas - 雷达可视化画布类
 * 支持顶装(ceiling)和侧装(side)两种模式
 * 显示网格、目标点、运动轨迹、雷达位置
 */
class RadarCanvas {
    constructor(canvasId, options = {}) {
        this.canvas = document.getElementById(canvasId);
        if (!this.canvas) {
            throw new Error(`Canvas element with id '${canvasId}' not found`);
        }

        this.ctx = this.canvas.getContext('2d');

        // 配置选项
        this.options = {
            mountMode: options.mountMode || 'side', // 'side' 或 'ceiling'
            roomWidth: options.roomWidth || 6,      // 房间宽度(米)
            roomDepth: options.roomDepth || 8,      // 房间深度(米)
            roomHeight: options.roomHeight || 3,    // 房间高度(米)
            gridSize: options.gridSize || 0.5,      // 网格大小(米)
            trailLength: options.trailLength || 60, // 轨迹长度(秒)
            colors: {
                grid: '#e0e0e0',
                gridText: '#999',
                radar: '#2196F3',
                radarRange: 'rgba(33, 150, 243, 0.1)',
                target: '#4CAF50',
                targetSelected: '#FF5722',
                trail: 'rgba(76, 175, 80, 0.6)',
                trailFade: 'rgba(76, 175, 80, 0.1)',
                background: '#fafafa',
                border: '#ddd'
            },
            ...options
        };

        // 状态
        this.targets = new Map();      // 目标 Map<id, target>
        this.selectedTarget = null;    // 选中的目标ID
        this.radarPosition = { x: 0, y: 0, z: 0 }; // 雷达位置
        this.animationId = null;
        this.lastFrameTime = 0;

        // 缩放和平移
        this.scale = 1;
        this.offsetX = 0;
        this.offsetY = 0;
        this.isDragging = false;
        this.lastMouseX = 0;
        this.lastMouseY = 0;

        // 初始化
        this.init();
    }

    /**
     * 初始化画布
     */
    init() {
        this.resize();
        this.setupEventListeners();
        this.calculateRadarPosition();

        // 监听窗口大小变化
        window.addEventListener('resize', () => this.resize());
    }

    /**
     * 调整画布大小
     */
    resize() {
        const container = this.canvas.parentElement;
        this.canvas.width = container.clientWidth;
        this.canvas.height = container.clientHeight;
        this.calculateTransform();
        this.render();
    }

    /**
     * 计算雷达在画布上的位置
     */
    calculateRadarPosition() {
        const width = this.canvas.width;
        const height = this.canvas.height;

        if (this.options.mountMode === 'side') {
            // 侧装：雷达在左下角
            this.radarScreenPos = {
                x: width * 0.1,
                y: height * 0.9
            };
        } else {
            // 顶装：雷达在中心
            this.radarScreenPos = {
                x: width * 0.5,
                y: height * 0.5
            };
        }
    }

    /**
     * 计算坐标转换参数
     */
    calculateTransform() {
        const width = this.canvas.width;
        const height = this.canvas.height;

        // 计算合适的缩放比例
        const padding = 60;
        const availableWidth = width - padding * 2;
        const availableHeight = height - padding * 2;

        if (this.options.mountMode === 'side') {
            // 侧装：X轴水平向右，Z轴垂直向上
            const scaleX = availableWidth / this.options.roomDepth;
            const scaleY = availableHeight / this.options.roomHeight;
            this.scale = Math.min(scaleX, scaleY) * 0.9;

            // 雷达在左下角
            this.radarScreenPos = {
                x: padding,
                y: height - padding
            };
        } else {
            // 顶装：X轴水平，Y轴垂直
            const scaleX = availableWidth / this.options.roomWidth;
            const scaleY = availableHeight / this.options.roomDepth;
            this.scale = Math.min(scaleX, scaleY) * 0.9;

            // 雷达在中心
            this.radarScreenPos = {
                x: width / 2,
                y: height / 2
            };
        }
    }

    /**
     * 世界坐标转换为屏幕坐标
     */
    worldToScreen(x, y, z) {
        if (this.options.mountMode === 'side') {
            // 侧装：显示 X(距离) 和 Z(高度)
            // 雷达在 (0, 0, 0)，X向前，Z向上
            return {
                x: this.radarScreenPos.x + x * this.scale,
                y: this.radarScreenPos.y - z * this.scale
            };
        } else {
            // 顶装：显示 X 和 Y
            // 雷达在中心，X向右，Y向下(或根据实际坐标系调整)
            return {
                x: this.radarScreenPos.x + x * this.scale,
                y: this.radarScreenPos.y + y * this.scale
            };
        }
    }

    /**
     * 屏幕坐标转换为世界坐标
     */
    screenToWorld(screenX, screenY) {
        if (this.options.mountMode === 'side') {
            return {
                x: (screenX - this.radarScreenPos.x) / this.scale,
                y: 0,
                z: (this.radarScreenPos.y - screenY) / this.scale
            };
        } else {
            return {
                x: (screenX - this.radarScreenPos.x) / this.scale,
                y: (screenY - this.radarScreenPos.y) / this.scale,
                z: 0
            };
        }
    }

    /**
     * 设置事件监听
     */
    setupEventListeners() {
        // 鼠标滚轮缩放
        this.canvas.addEventListener('wheel', (e) => {
            e.preventDefault();
            const delta = e.deltaY > 0 ? 0.9 : 1.1;
            this.scale *= delta;
            this.scale = Math.max(10, Math.min(500, this.scale)); // 限制缩放范围
            this.render();
        });

        // 鼠标拖拽平移
        this.canvas.addEventListener('mousedown', (e) => {
            this.isDragging = true;
            this.lastMouseX = e.clientX;
            this.lastMouseY = e.clientY;
        });

        this.canvas.addEventListener('mousemove', (e) => {
            if (this.isDragging) {
                const dx = e.clientX - this.lastMouseX;
                const dy = e.clientY - this.lastMouseY;
                this.offsetX += dx;
                this.offsetY += dy;
                this.lastMouseX = e.clientX;
                this.lastMouseY = e.clientY;
                this.render();
            }

            // 更新坐标显示
            const rect = this.canvas.getBoundingClientRect();
            const screenX = e.clientX - rect.left;
            const screenY = e.clientY - rect.top;
            const worldPos = this.screenToWorld(screenX - this.offsetX, screenY - this.offsetY);
            this.updateCoordinateDisplay(worldPos);
        });

        this.canvas.addEventListener('mouseup', () => {
            this.isDragging = false;
        });

        this.canvas.addEventListener('mouseleave', () => {
            this.isDragging = false;
        });

        // 点击选择目标
        this.canvas.addEventListener('click', (e) => {
            const rect = this.canvas.getBoundingClientRect();
            const screenX = e.clientX - rect.left;
            const screenY = e.clientY - rect.top;
            this.handleClick(screenX, screenY);
        });
    }

    /**
     * 更新坐标显示
     */
    updateCoordinateDisplay(pos) {
        const coordElement = document.getElementById('coordinates');
        if (coordElement) {
            if (this.options.mountMode === 'side') {
                coordElement.textContent = `X: ${pos.x.toFixed(2)}m Z: ${pos.z.toFixed(2)}m`;
            } else {
                coordElement.textContent = `X: ${pos.x.toFixed(2)}m Y: ${pos.y.toFixed(2)}m`;
            }
        }
    }

    /**
     * 处理点击事件
     */
    handleClick(screenX, screenY) {
        let closestTarget = null;
        let closestDistance = Infinity;
        const clickRadius = 20; // 点击检测半径

        this.targets.forEach((target, id) => {
            const screenPos = this.worldToScreen(target.x, target.y, target.z);
            const distance = Math.hypot(screenPos.x - screenX, screenPos.y - screenY);
            if (distance < clickRadius && distance < closestDistance) {
                closestDistance = distance;
                closestTarget = id;
            }
        });

        if (closestTarget !== null) {
            this.selectTarget(closestTarget);
        } else {
            this.deselectTarget();
        }
    }

    /**
     * 选择目标
     */
    selectTarget(targetId) {
        this.selectedTarget = targetId;
        this.render();

        // 触发选择事件
        const event = new CustomEvent('targetSelected', {
            detail: { targetId, target: this.targets.get(targetId) }
        });
        this.canvas.dispatchEvent(event);
    }

    /**
     * 取消选择
     */
    deselectTarget() {
        this.selectedTarget = null;
        this.render();

        const event = new CustomEvent('targetDeselected');
        this.canvas.dispatchEvent(event);
    }

    /**
     * 更新目标数据
     */
    updateTargets(targetList) {
        const now = Date.now();

        targetList.forEach(target => {
            const existing = this.targets.get(target.id);
            if (existing) {
                // 更新现有目标，保留轨迹
                if (!existing.trail) existing.trail = [];
                existing.trail.push({
                    x: existing.x,
                    y: existing.y,
                    z: existing.z,
                    timestamp: now
                });

                // 限制轨迹长度
                const maxTrailLength = this.options.trailLength * 10; // 假设10Hz
                if (existing.trail.length > maxTrailLength) {
                    existing.trail.shift();
                }

                // 更新属性
                Object.assign(existing, target);
                existing.lastUpdate = now;
            } else {
                // 新目标
                this.targets.set(target.id, {
                    ...target,
                    trail: [],
                    firstSeen: now,
                    lastUpdate: now
                });
            }
        });

        // 清理过期目标(超过2秒未更新)
        this.targets.forEach((target, id) => {
            if (now - target.lastUpdate > 2000) {
                this.targets.delete(id);
                if (this.selectedTarget === id) {
                    this.deselectTarget();
                }
            }
        });
    }

    /**
     * 设置安装模式
     */
    setMountMode(mode) {
        if (this.options.mountMode !== mode) {
            this.options.mountMode = mode;
            this.targets.clear(); // 清除目标，避免坐标混乱
            this.deselectTarget();
            this.calculateTransform();
            this.render();
        }
    }

    /**
     * 设置房间尺寸
     */
    setRoomSize(width, depth, height) {
        this.options.roomWidth = width;
        this.options.roomDepth = depth;
        if (height) this.options.roomHeight = height;
        this.calculateTransform();
        this.render();
    }

    /**
     * 渲染画布
     */
    render() {
        const ctx = this.ctx;
        const width = this.canvas.width;
        const height = this.canvas.height;

        // 清空画布
        ctx.fillStyle = this.options.colors.background;
        ctx.fillRect(0, 0, width, height);

        ctx.save();
        ctx.translate(this.offsetX, this.offsetY);

        // 绘制网格
        this.drawGrid(ctx);

        // 绘制雷达范围
        this.drawRadarRange(ctx);

        // 绘制轨迹
        this.drawTrails(ctx);

        // 绘制目标
        this.drawTargets(ctx);

        // 绘制雷达位置
        this.drawRadar(ctx);

        ctx.restore();
    }

    /**
     * 绘制网格
     */
    drawGrid(ctx) {
        const { roomWidth, roomDepth, roomHeight, gridSize, mountMode } = this.options;
        const colors = this.options.colors;

        ctx.strokeStyle = colors.grid;
        ctx.lineWidth = 1;
        ctx.font = '12px Arial';
        ctx.fillStyle = colors.gridText;

        if (mountMode === 'side') {
            // 侧装网格：X(水平) 和 Z(垂直)
            const maxX = roomDepth;
            const maxZ = roomHeight;

            // 垂直线 (X)
            for (let x = 0; x <= maxX; x += gridSize) {
                const pos = this.worldToScreen(x, 0, 0);
                ctx.beginPath();
                ctx.moveTo(pos.x, this.radarScreenPos.y);
                ctx.lineTo(pos.x, this.radarScreenPos.y - maxZ * this.scale);
                ctx.stroke();

                // 标签
                ctx.fillText(`${x.toFixed(1)}m`, pos.x - 10, this.radarScreenPos.y + 15);
            }

            // 水平线 (Z)
            for (let z = 0; z <= maxZ; z += gridSize) {
                const pos = this.worldToScreen(0, 0, z);
                ctx.beginPath();
                ctx.moveTo(this.radarScreenPos.x, pos.y);
                ctx.lineTo(this.radarScreenPos.x + maxX * this.scale, pos.y);
                ctx.stroke();

                // 标签
                ctx.fillText(`${z.toFixed(1)}m`, this.radarScreenPos.x - 35, pos.y + 4);
            }

            // 坐标轴标签
            ctx.font = 'bold 14px Arial';
            ctx.fillStyle = '#666';
            ctx.fillText('距离 (X)', this.radarScreenPos.x + maxX * this.scale / 2 - 30, this.radarScreenPos.y + 30);
            ctx.fillText('高度 (Z)', this.radarScreenPos.x - 50, this.radarScreenPos.y - maxZ * this.scale / 2);
        } else {
            // 顶装网格：X 和 Y
            const halfWidth = roomWidth / 2;
            const halfDepth = roomDepth / 2;

            // 垂直线 (X)
            for (let x = -halfWidth; x <= halfWidth; x += gridSize) {
                const pos = this.worldToScreen(x, -halfDepth, 0);
                const endPos = this.worldToScreen(x, halfDepth, 0);
                ctx.beginPath();
                ctx.moveTo(pos.x, pos.y);
                ctx.lineTo(endPos.x, endPos.y);
                ctx.stroke();

                if (Math.abs(x) > 0.01) {
                    ctx.fillText(`${x.toFixed(1)}m`, pos.x - 15, pos.y - 5);
                }
            }

            // 水平线 (Y)
            for (let y = -halfDepth; y <= halfDepth; y += gridSize) {
                const pos = this.worldToScreen(-halfWidth, y, 0);
                const endPos = this.worldToScreen(halfWidth, y, 0);
                ctx.beginPath();
                ctx.moveTo(pos.x, pos.y);
                ctx.lineTo(endPos.x, endPos.y);
                ctx.stroke();

                if (Math.abs(y) > 0.01) {
                    ctx.fillText(`${y.toFixed(1)}m`, pos.x - 40, pos.y + 4);
                }
            }

            // 坐标轴标签
            ctx.font = 'bold 14px Arial';
            ctx.fillStyle = '#666';
            ctx.fillText('X', this.radarScreenPos.x + halfWidth * this.scale - 10, this.radarScreenPos.y + 20);
            ctx.fillText('Y', this.radarScreenPos.x - 20, this.radarScreenPos.y - halfDepth * this.scale + 15);
        }
    }

    /**
     * 绘制雷达探测范围
     */
    drawRadarRange(ctx) {
        const colors = this.options.colors;
        ctx.fillStyle = colors.radarRange;

        if (this.options.mountMode === 'side') {
            // 侧装：扇形区域
            const range = Math.min(this.options.roomDepth, this.options.roomHeight * 2) * this.scale;
            ctx.beginPath();
            ctx.moveTo(this.radarScreenPos.x, this.radarScreenPos.y);
            ctx.arc(this.radarScreenPos.x, this.radarScreenPos.y, range, -Math.PI / 3, 0);
            ctx.closePath();
            ctx.fill();
        } else {
            // 顶装：圆形区域
            const range = Math.min(this.options.roomWidth, this.options.roomDepth) / 2 * this.scale;
            ctx.beginPath();
            ctx.arc(this.radarScreenPos.x, this.radarScreenPos.y, range, 0, Math.PI * 2);
            ctx.fill();
        }
    }

    /**
     * 绘制轨迹
     */
    drawTrails(ctx) {
        const colors = this.options.colors;
        const now = Date.now();

        this.targets.forEach((target, id) => {
            if (!target.trail || target.trail.length < 2) return;

            // 绘制轨迹线
            for (let i = 1; i < target.trail.length; i++) {
                const point = target.trail[i];
                const prevPoint = target.trail[i - 1];
                const age = now - point.timestamp;
                const maxAge = this.options.trailLength * 1000;
                const alpha = Math.max(0, 1 - age / maxAge);

                const pos = this.worldToScreen(point.x, point.y, point.z);
                const prevPos = this.worldToScreen(prevPoint.x, prevPoint.y, prevPoint.z);

                ctx.strokeStyle = `rgba(76, 175, 80, ${alpha * 0.6})`;
                ctx.lineWidth = 2;
                ctx.beginPath();
                ctx.moveTo(prevPos.x, prevPos.y);
                ctx.lineTo(pos.x, pos.y);
                ctx.stroke();
            }
        });
    }

    /**
     * 绘制目标
     */
    drawTargets(ctx) {
        const colors = this.options.colors;

        this.targets.forEach((target, id) => {
            const pos = this.worldToScreen(target.x, target.y, target.z);
            const isSelected = id === this.selectedTarget;

            // 目标圆圈
            ctx.beginPath();
            ctx.arc(pos.x, pos.y, isSelected ? 10 : 6, 0, Math.PI * 2);
            ctx.fillStyle = isSelected ? colors.targetSelected : colors.target;
            ctx.fill();

            // 选中时绘制外圈
            if (isSelected) {
                ctx.strokeStyle = colors.targetSelected;
                ctx.lineWidth = 2;
                ctx.beginPath();
                ctx.arc(pos.x, pos.y, 14, 0, Math.PI * 2);
                ctx.stroke();
            }

            // 速度向量
            if (target.vx !== undefined && target.vy !== undefined) {
                const vx = this.options.mountMode === 'side' ? target.vx : target.vx;
                const vy = this.options.mountMode === 'side' ? target.vz : target.vy;
                const vScale = 0.5; // 速度缩放

                ctx.strokeStyle = isSelected ? colors.targetSelected : colors.target;
                ctx.lineWidth = 2;
                ctx.beginPath();
                ctx.moveTo(pos.x, pos.y);
                ctx.lineTo(pos.x + vx * vScale * this.scale, pos.y - vy * vScale * this.scale);
                ctx.stroke();
            }

            // 目标ID标签
            ctx.fillStyle = '#333';
            ctx.font = '12px Arial';
            ctx.fillText(`ID:${id}`, pos.x + 10, pos.y - 10);

            // 选中时显示详细信息
            if (isSelected) {
                this.drawTargetInfo(ctx, target, pos);
            }
        });
    }

    /**
     * 绘制目标详细信息
     */
    drawTargetInfo(ctx, target, pos) {
        const lines = [
            `ID: ${target.id}`,
            `X: ${target.x.toFixed(2)}m`,
            this.options.mountMode === 'side'
                ? `Z: ${target.z.toFixed(2)}m`
                : `Y: ${target.y.toFixed(2)}m`,
            `速度: ${Math.hypot(target.vx || 0, target.vy || 0).toFixed(2)}m/s`
        ];

        const lineHeight = 16;
        const padding = 8;
        const boxWidth = 100;
        const boxHeight = lines.length * lineHeight + padding * 2;

        // 信息框位置（避免超出画布）
        let boxX = pos.x + 20;
        let boxY = pos.y - boxHeight / 2;

        if (boxX + boxWidth > this.canvas.width) {
            boxX = pos.x - boxWidth - 20;
        }
        if (boxY < 0) boxY = 10;
        if (boxY + boxHeight > this.canvas.height) {
            boxY = this.canvas.height - boxHeight - 10;
        }

        // 绘制背景
        ctx.fillStyle = 'rgba(255, 255, 255, 0.95)';
        ctx.strokeStyle = '#ddd';
        ctx.lineWidth = 1;
        ctx.fillRect(boxX, boxY, boxWidth, boxHeight);
        ctx.strokeRect(boxX, boxY, boxWidth, boxHeight);

        // 绘制文字
        ctx.fillStyle = '#333';
        ctx.font = '12px Arial';
        lines.forEach((line, i) => {
            ctx.fillText(line, boxX + padding, boxY + padding + (i + 1) * lineHeight - 4);
        });
    }

    /**
     * 绘制雷达位置
     */
    drawRadar(ctx) {
        const colors = this.options.colors;
        const size = 12;

        // 雷达图标（三角形）
        ctx.fillStyle = colors.radar;
        ctx.beginPath();

        if (this.options.mountMode === 'side') {
            // 侧装：朝右上的三角形
            ctx.moveTo(this.radarScreenPos.x, this.radarScreenPos.y - size);
            ctx.lineTo(this.radarScreenPos.x + size, this.radarScreenPos.y);
            ctx.lineTo(this.radarScreenPos.x, this.radarScreenPos.y);
        } else {
            // 顶装：朝上的三角形
            ctx.moveTo(this.radarScreenPos.x, this.radarScreenPos.y - size);
            ctx.lineTo(this.radarScreenPos.x - size * 0.7, this.radarScreenPos.y + size * 0.5);
            ctx.lineTo(this.radarScreenPos.x + size * 0.7, this.radarScreenPos.y + size * 0.5);
        }

        ctx.closePath();
        ctx.fill();

        // 雷达标签
        ctx.fillStyle = colors.radar;
        ctx.font = 'bold 12px Arial';
        ctx.fillText('RADAR', this.radarScreenPos.x - 20, this.radarScreenPos.y + 25);
    }

    /**
     * 开始动画循环
     */
    start() {
        if (this.animationId) return;

        const animate = (timestamp) => {
            this.render();
            this.animationId = requestAnimationFrame(animate);
        };

        this.animationId = requestAnimationFrame(animate);
    }

    /**
     * 停止动画循环
     */
    stop() {
        if (this.animationId) {
            cancelAnimationFrame(this.animationId);
            this.animationId = null;
        }
    }

    /**
     * 清除所有目标
     */
    clearTargets() {
        this.targets.clear();
        this.selectedTarget = null;
        this.render();
    }

    /**
     * 获取当前目标数量
     */
    getTargetCount() {
        return this.targets.size;
    }

    /**
     * 获取选中目标
     */
    getSelectedTarget() {
        return this.selectedTarget ? this.targets.get(this.selectedTarget) : null;
    }
}

// 导出
if (typeof module !== 'undefined' && module.exports) {
    module.exports = RadarCanvas;
}
