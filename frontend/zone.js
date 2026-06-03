/**
 * ZoneManager - 检测区域数据管理
 * 支持最多 8 个多边形检测区域
 * 负责：数据存储、验证、触发状态管理
 */

class ZoneManager {
    constructor(options = {}) {
        // 配置
        this.maxZones = options.maxZones || 8;           // 最大区域数
        this.maxPointsPerZone = options.maxPoints || 10; // 每个区域最大顶点数
        this.minPointsPerZone = 3;                       // 最小顶点数
        this.coordRange = options.coordRange || 20;      // 坐标范围 ±20m
        
        // 区域数据 Map: id -> Zone
        this.zones = new Map();
        
        // 下一个 ID（简单自增）
        this.nextId = 1;
        
        // 默认颜色池
        this.defaultColors = [
            '#ff6b6b', '#51cf66', '#339af0', '#fcc419',
            '#cc5de8', '#ff922b', '#20c997', '#f06595'
        ];
        
        // 统计
        this.triggerCount = 0; // 触发次数统计
        
        // 回调
        this.onZonesUpdate = null; // (zones) => void
        this.onTriggerChange = null; // (zoneId, triggered) => void
    }
    
    /**
     * 验证单个区域数据
     * @param {object} zone - 区域数据
     * @returns {boolean} 是否有效
     */
    _validateZone(zone) {
        // 基础检查
        if (!zone || typeof zone !== 'object') return false;
        
        // 必填字段
        if (typeof zone.id !== 'number') return false;
        if (!zone.name || typeof zone.name !== 'string') return false;
        if (!Array.isArray(zone.points)) return false;
        
        // 顶点数量
        const pointCount = zone.points.length;
        if (pointCount < this.minPointsPerZone || pointCount > this.maxPointsPerZone) {
            console.warn(`[ZoneManager] 区域 "${zone.name}" 顶点数(${pointCount})超出范围`);
            return false;
        }
        
        // 验证每个顶点
        for (const point of zone.points) {
            if (!Array.isArray(point) || point.length !== 2) return false;
            
            const [x, y] = point;
            if (typeof x !== 'number' || typeof y !== 'number') return false;
            
            // 坐标范围检查
            if (Math.abs(x) > this.coordRange || Math.abs(y) > this.coordRange) {
                console.warn(`[ZoneManager] 区域 "${zone.name}" 坐标超出范围: (${x}, ${y})`);
                return false;
            }
        }
        
        // 颜色格式（可选，有默认值）
        if (zone.color && !/^#[0-9a-f]{6}$/i.test(zone.color)) {
            console.warn(`[ZoneManager] 区域 "${zone.name}" 颜色格式无效: ${zone.color}`);
            return false;
        }
        
        return true;
    }
    
    /**
     * 添加区域
     * @param {object} zoneData - 区域数据（不含 id）
     * @returns {object|null} 添加的区域，失败返回 null
     */
    addZone(zoneData) {
        // 检查数量限制
        if (this.zones.size >= this.maxZones) {
            console.warn(`[ZoneManager] 区域数量已达上限 (${this.maxZones})`);
            return null;
        }
        
        // 生成 ID
        const zone = {
            id: this.nextId++,
            name: zoneData.name || `区域${this.nextId}`,
            points: zoneData.points || [],
            color: zoneData.color || this._getNextColor(),
            enabled: zoneData.enabled !== false,
            triggered: false,
            lastTriggerTime: 0,
            triggerCount: 0
        };
        
        // 验证
        if (!this._validateZone(zone)) {
            console.error('[ZoneManager] 区域数据验证失败');
            return null;
        }
        
        // 保存
        this.zones.set(zone.id, zone);
        
        // 通知更新
        if (this.onZonesUpdate) {
            this.onZonesUpdate(this.getZonesArray());
        }
        
        console.log(`[ZoneManager] 添加区域: ${zone.name} (ID: ${zone.id})`);
        return zone;
    }
    
    /**
     * 删除区域
     * @param {number} zoneId - 区域 ID
     * @returns {boolean} 是否成功
     */
    deleteZone(zoneId) {
        if (!this.zones.has(zoneId)) {
            console.warn(`[ZoneManager] 区域不存在: ${zoneId}`);
            return false;
        }
        
        const zone = this.zones.get(zoneId);
        this.zones.delete(zoneId);
        
        console.log(`[ZoneManager] 删除区域: ${zone.name} (ID: ${zoneId})`);
        
        // 通知更新
        if (this.onZonesUpdate) {
            this.onZonesUpdate(this.getZonesArray());
        }
        
        return true;
    }
    
    /**
     * 删除区域（别名）
     * @param {number} zoneId - 区域 ID
     * @returns {boolean} 是否成功
     */
    removeZone(zoneId) {
        return this.deleteZone(zoneId);
    }
    
    /**
     * 通知更新（用于编辑后刷新）
     */
    notifyUpdate() {
        if (this.onZonesUpdate) {
            this.onZonesUpdate(this.getZonesArray());
        }
    }
    
    /**
     * 更新区域
     * @param {number} zoneId - 区域 ID
     * @param {object} updates - 更新的字段
     * @returns {boolean} 是否成功
     */
    updateZone(zoneId, updates) {
        if (!this.zones.has(zoneId)) {
            console.warn(`[ZoneManager] 区域不存在: ${zoneId}`);
            return false;
        }
        
        const zone = this.zones.get(zoneId);
        
        // 合并更新
        if (updates.name !== undefined) zone.name = updates.name;
        if (updates.points !== undefined) {
            // 验证新顶点
            const tempZone = { ...zone, points: updates.points };
            if (!this._validateZone(tempZone)) {
                console.error('[ZoneManager] 更新后的区域数据验证失败');
                return false;
            }
            zone.points = updates.points;
        }
        if (updates.color !== undefined) zone.color = updates.color;
        if (updates.enabled !== undefined) zone.enabled = updates.enabled;
        
        // 通知更新
        if (this.onZonesUpdate) {
            this.onZonesUpdate(this.getZonesArray());
        }
        
        return true;
    }
    
    /**
     * 批量设置区域（从后端加载）
     * @param {array} zonesArray - 区域数组
     */
    setZones(zonesArray) {
        if (!Array.isArray(zonesArray)) {
            console.error('[ZoneManager] setZones 参数必须是数组');
            return;
        }
        
        // 清空现有区域
        this.zones.clear();
        
        // 添加新区域
        let successCount = 0;
        let maxId = 0;
        
        for (const zoneData of zonesArray) {
            // 兼容后端返回的格式（可能已有 id）
            const zone = {
                id: zoneData.id || this.nextId++,
                name: zoneData.name || '未命名',
                points: zoneData.points || [],
                color: zoneData.color || this._getNextColor(),
                enabled: zoneData.enabled !== false,
                triggered: false,
                lastTriggerTime: 0,
                triggerCount: 0
            };
            
            if (this._validateZone(zone)) {
                this.zones.set(zone.id, zone);
                successCount++;
                
                // 更新 nextId
                if (zone.id >= maxId) {
                    maxId = zone.id + 1;
                }
            } else {
                console.warn(`[ZoneManager] 跳过无效区域:`, zoneData);
            }
        }
        
        // 更新 nextId
        if (maxId > this.nextId) {
            this.nextId = maxId;
        }
        
        console.log(`[ZoneManager] 加载 ${successCount}/${zonesArray.length} 个区域`);
        
        // 通知更新
        if (this.onZonesUpdate) {
            this.onZonesUpdate(this.getZonesArray());
        }
    }
    
    /**
     * 获取所有区域数组
     * @returns {array} 区域数组
     */
    getZonesArray() {
        return Array.from(this.zones.values());
    }
    
    /**
     * 获取单个区域
     * @param {number} zoneId - 区域 ID
     * @returns {object|null} 区域数据
     */
    getZone(zoneId) {
        return this.zones.get(zoneId) || null;
    }
    
    /**
     * 获取区域数量
     * @returns {number}
     */
    getZoneCount() {
        return this.zones.size;
    }
    
    /**
     * 检查是否还能添加区域
     * @returns {boolean}
     */
    canAddZone() {
        return this.zones.size < this.maxZones;
    }
    
    /**
     * 更新区域触发状态
     * @param {array} triggerStates - [[zoneId, triggered], ...]
     */
    updateTriggerStates(triggerStates) {
        if (!Array.isArray(triggerStates)) return;
        
        let hasChanges = false;
        
        for (const [zoneId, triggered] of triggerStates) {
            const zone = this.zones.get(zoneId);
            if (!zone) continue;
            
            const wasTriggered = zone.triggered;
            zone.triggered = !!triggered;
            
            if (triggered) {
                zone.lastTriggerTime = Date.now();
                zone.triggerCount++;
                this.triggerCount++;
                
                // 只在状态变化时通知
                if (!wasTriggered && this.onTriggerChange) {
                    this.onTriggerChange(zoneId, true);
                }
            } else {
                // 只在状态变化时通知
                if (wasTriggered && this.onTriggerChange) {
                    this.onTriggerChange(zoneId, false);
                }
            }
            
            if (wasTriggered !== zone.triggered) {
                hasChanges = true;
            }
        }
        
        // 如果有变化，通知更新
        if (hasChanges && this.onZonesUpdate) {
            this.onZonesUpdate(this.getZonesArray());
        }
    }
    
    /**
     * 检查点是否在任意区域内
     * @param {number} x - X 坐标
     * @param {number} y - Y 坐标
     * @returns {array} 触发的区域 ID 列表
     */
    checkPoint(x, y) {
        const triggeredZones = [];
        
        for (const zone of this.zones.values()) {
            if (!zone.enabled) continue;
            
            if (this.isPointInPolygon(x, y, zone.points)) {
                triggeredZones.push(zone.id);
            }
        }
        
        return triggeredZones;
    }
    
    /**
     * Ray Casting 算法：判断点是否在多边形内
     * @param {number} px - 点 X 坐标
     * @param {number} py - 点 Y 坐标
     * @param {array} polygon - 多边形顶点 [[x,y], ...]
     * @returns {boolean}
     */
    isPointInPolygon(px, py, polygon) {
        if (!polygon || polygon.length < 3) return false;
        
        let inside = false;
        const epsilon = 0.001; // 浮点数容差
        
        for (let i = 0, j = polygon.length - 1; i < polygon.length; j = i++) {
            const [xi, yi] = polygon[i];
            const [xj, yj] = polygon[j];
            
            // 检查点是否在顶点上（容差处理）
            if (Math.abs(px - xi) < epsilon && Math.abs(py - yi) < epsilon) {
                return true;
            }
            
            // 射线交叉判断
            const intersect = ((yi > py) !== (yj > py)) &&
                (px < (xj - xi) * (py - yi) / (yj - yi) + xi);
            
            if (intersect) inside = !inside;
        }
        
        return inside;
    }
    
    /**
     * 获取下一个默认颜色
     * @returns {string} 颜色值
     */
    _getNextColor() {
        const index = (this.zones.size) % this.defaultColors.length;
        return this.defaultColors[index];
    }
    
    /**
     * 清空所有区域
     */
    clear() {
        this.zones.clear();
        this.triggerCount = 0;
        
        if (this.onZonesUpdate) {
            this.onZonesUpdate([]);
        }
    }
    
    /**
     * 导出配置（用于保存到后端）
     * @returns {array} 可序列化的区域数组
     */
    exportConfig() {
        return this.getZonesArray().map(zone => ({
            id: zone.id,
            name: zone.name,
            point_count: zone.points.length,
            points: zone.points,
            color: zone.color,
            enabled: zone.enabled
        }));
    }
    
    /**
     * 获取统计信息
     * @returns {object}
     */
    getStats() {
        const zones = this.getZonesArray();
        const triggeredCount = zones.filter(z => z.triggered).length;
        
        return {
            totalZones: zones.length,
            enabledZones: zones.filter(z => z.enabled).length,
            triggeredZones: triggeredCount,
            totalTriggers: this.triggerCount
        };
    }
}
