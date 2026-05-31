/**
 * RadarDataManager - 雷达数据处理
 * 接收 WebSocket 推送的 radar_data，维护目标状态和轨迹
 */

class RadarDataManager {
    constructor(options = {}) {
        // 配置
        this.trailLength = options.trailLength || 60; // 轨迹帧数（6秒 @10Hz）
        this.targetTimeout = options.targetTimeout || 1000; // 目标超时 1s

        // 目标数据 Map: id -> { x, y, z, speed, snr, confidence, trail: [{x,y,z}], lastUpdate }
        this.targets = new Map();

        // 统计
        this.frameCount = 0;
        this.lastFrameTime = 0;
        this.fps = 0;
        this.latency = 0;
        this._fpsFrames = 0;
        this._fpsLastCalc = performance.now();

        // 回调
        this.onTargetsUpdate = null; // (targets) => void
        this.onStatsUpdate = null;   // (stats) => void
    }

    /**
     * 处理 WebSocket 推送的雷达数据
     * @param {object} data - { type: 'radar_data', timestamp, frame_id, targets: [...], target_count }
     */
    processRadarData(data) {
        if (data.type !== 'radar_data') return;

        const now = performance.now();

        // 计算帧间隔作为延迟指标（前后端时钟不同步，不能直接用 timestamp 差值）
        if (this.lastFrameTime > 0) {
            const frameInterval = now - this.lastFrameTime;
            // 只更新合理的值（避免异常跳变）
            if (frameInterval > 0 && frameInterval < 5000) {
                this.latency = frameInterval;
            }
        }

        // 更新帧计数
        this.frameCount = data.frame_id || this.frameCount + 1;
        this.lastFrameTime = now;

        // FPS 计算（每秒更新一次）
        this._fpsFrames++;
        if (now - this._fpsLastCalc >= 1000) {
            this.fps = this._fpsFrames;
            this._fpsFrames = 0;
            this._fpsLastCalc = now;
        }

        // 处理目标数据
        const incomingIds = new Set();
        const targetList = data.targets || [];

        for (const t of targetList) {
            incomingIds.add(t.id);
            this._updateTarget(t);
        }

        // 清理不再出现的目标
        this._cleanupTargets(incomingIds, now);

        // 通知更新
        if (this.onTargetsUpdate) {
            this.onTargetsUpdate(this.getTargetsArray());
        }

        if (this.onStatsUpdate) {
            this.onStatsUpdate(this.getStats());
        }
    }

    /**
     * 更新单个目标
     */
    _updateTarget(targetData) {
        const id = targetData.id;
        const existing = this.targets.get(id);

        if (existing) {
            // 更新位置
            existing.x = targetData.x;
            existing.y = targetData.y;
            existing.z = targetData.z || 0;
            existing.speed = targetData.speed || 0;
            existing.snr = targetData.snr || 0;
            existing.confidence = targetData.confidence || 0;
            existing.lastUpdate = performance.now();

            // 追加轨迹点
            existing.trail.push({ x: targetData.x, y: targetData.y, z: targetData.z || 0 });
            if (existing.trail.length > this.trailLength) {
                existing.trail.splice(0, existing.trail.length - this.trailLength);
            }
        } else {
            // 新目标
            this.targets.set(id, {
                id: id,
                x: targetData.x,
                y: targetData.y,
                z: targetData.z || 0,
                speed: targetData.speed || 0,
                snr: targetData.snr || 0,
                confidence: targetData.confidence || 0,
                trail: [{ x: targetData.x, y: targetData.y, z: targetData.z || 0 }],
                lastUpdate: performance.now()
            });
        }
    }

    /**
     * 清理过期目标
     */
    _cleanupTargets(activeIds, now) {
        for (const [id, target] of this.targets) {
            if (!activeIds.has(id)) {
                if (now - target.lastUpdate > this.targetTimeout) {
                    this.targets.delete(id);
                }
            }
        }
    }

    /**
     * 获取目标数组
     */
    getTargetsArray() {
        return Array.from(this.targets.values());
    }

    /**
     * 获取目标数量
     */
    getTargetCount() {
        return this.targets.size;
    }

    /**
     * 获取统计数据
     */
    getStats() {
        return {
            frameCount: this.frameCount,
            fps: this.fps,
            latency: Math.round(this.latency),
            targetCount: this.targets.size
        };
    }

    /**
     * 设置轨迹长度
     */
    setTrailLength(length) {
        this.trailLength = Math.max(0, length);
        // 裁剪现有轨迹
        for (const [, target] of this.targets) {
            if (target.trail.length > this.trailLength) {
                target.trail.splice(0, target.trail.length - this.trailLength);
            }
        }
    }

    /**
     * 清空所有目标
     */
    clearTargets() {
        this.targets.clear();
    }

    /**
     * 重置统计
     */
    resetStats() {
        this.frameCount = 0;
        this.fps = 0;
        this.latency = 0;
        this._fpsFrames = 0;
        this._fpsLastCalc = performance.now();
    }
}
