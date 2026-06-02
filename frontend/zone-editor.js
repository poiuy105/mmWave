/**
 * ZoneEditor - 区域编辑器
 * 支持桌面端（鼠标）和移动端（触摸）绘制多边形
 */

class ZoneEditor {
    constructor(radarCanvas, zoneManager, api) {
        // radarCanvas 是 RadarCanvas 对象，需要获取其内部的 canvas DOM 元素
        this.radarCanvas = radarCanvas;
        this.canvas = radarCanvas.canvas; // Canvas DOM 元素
        this.zoneManager = zoneManager;
        this.api = api;
        
        // 编辑状态
        this.mode = 'view'; // view | draw | edit
        this.drawingPoints = []; // 正在绘制的顶点 [[x,y], ...]
        this.editingZoneId = null; // 正在编辑的区域 ID
        this.draggingVertexIndex = -1; // 正在拖拽的顶点索引
        
        // 创建工具栏
        this._createToolbar();
        
        // 绑定事件
        this._bindEvents();
        
        console.log('[ZoneEditor] 初始化完成');
    }
    
    /**
     * 创建浮动工具栏
     */
    _createToolbar() {
        const toolbar = document.createElement('div');
        toolbar.id = 'zoneToolbar';
        toolbar.className = 'zone-toolbar hidden';
        toolbar.innerHTML = `
            <button id="btnFinishZone" class="tool-btn">✓ 完成</button>
            <button id="btnSaveEdit" class="tool-btn" style="display:none">💾 保存</button>
            <button id="btnCancelZone" class="tool-btn">✕ 取消</button>
        `;
        document.body.appendChild(toolbar);
        
        // 绑定按钮事件
        document.getElementById('btnFinishZone').addEventListener('click', () => {
            this.finishDrawing();
        });
        
        document.getElementById('btnSaveEdit').addEventListener('click', () => {
            this._saveEditedZone();
            this.cancelEditing();
        });
        
        document.getElementById('btnCancelZone').addEventListener('click', () => {
            if (this.mode === 'draw') {
                this.cancelDrawing();
            } else if (this.mode === 'edit') {
                this.cancelEditing();
            }
        });
    }
    
    /**
     * 绑定事件
     */
    _bindEvents() {
        // 画布点击事件（统一处理鼠标和触摸）
        this.canvas.addEventListener('pointerdown', (e) => {
            e.preventDefault();
            
            if (this.mode === 'draw') {
                this._addPoint(e.clientX, e.clientY);
            } else if (this.mode === 'edit') {
                this._handleEditClick(e);
            }
        });
        
        // 鼠标移动（用于拖拽顶点）
        this.canvas.addEventListener('pointermove', (e) => {
            if (this.mode === 'edit' && this.draggingVertexIndex >= 0) {
                e.preventDefault();
                this._handleVertexDrag(e);
            }
        });
        
        // 鼠标释放（结束拖拽）
        this.canvas.addEventListener('pointerup', () => {
            if (this.mode === 'edit' && this.draggingVertexIndex >= 0) {
                this.draggingVertexIndex = -1;
                this._saveEditedZone();
            }
        });
        
        // 双击完成（仅桌面端）
        this.canvas.addEventListener('dblclick', (e) => {
            if (this.mode !== 'draw') return;
            e.preventDefault();
            this.finishDrawing();
        });
        
        // 右键取消（仅桌面端）
        this.canvas.addEventListener('contextmenu', (e) => {
            if (this.mode === 'draw') {
                e.preventDefault();
                this.cancelDrawing();
            } else if (this.mode === 'edit') {
                e.preventDefault();
                this.cancelEditing();
            }
        });
    }
    
    /**
     * 开始绘制
     */
    startDrawing() {
        // 检查是否达到上限
        if (!this.zoneManager.canAddZone()) {
            alert(`最多只能创建 ${this.zoneManager.maxZones} 个区域`);
            return;
        }
        
        this.mode = 'draw';
        this.drawingPoints = [];
        
        // 显示工具栏
        document.getElementById('zoneToolbar').classList.remove('hidden');
        document.getElementById('btnFinishZone').style.display = '';
        document.getElementById('btnSaveEdit').style.display = 'none';
        
        // 清空预览
        this.radarCanvas.clearPreview();
        
        // 提示
        console.log('[ZoneEditor] 进入绘制模式，点击画布添加顶点');
        
        // 在画布上显示提示
        this._showHint('点击添加顶点，双击/完成按钮结束');
    }
    
    /**
     * 添加顶点
     */
    _addPoint(clientX, clientY) {
        const rect = this.canvas.getBoundingClientRect();
        const x = clientX - rect.left;
        const y = clientY - rect.top;
        
        // 转换为世界坐标
        const worldPos = this.radarCanvas.screenToWorld(x, y);
        
        // 添加顶点
        this.drawingPoints.push([worldPos.x, worldPos.y]);
        
        // 更新预览
        this.radarCanvas.setPreviewPolygon(this.drawingPoints);
        
        console.log(`[ZoneEditor] 添加顶点 ${this.drawingPoints.length}: (${worldPos.x.toFixed(2)}, ${worldPos.y.toFixed(2)})`);
    }
    
    /**
     * 完成绘制
     */
    finishDrawing() {
        if (this.drawingPoints.length < 3) {
            alert(`至少需要 3 个顶点，当前只有 ${this.drawingPoints.length} 个`);
            return;
        }
        
        // 创建新区域
        const newZone = {
            name: `区域${this.zoneManager.getZoneCount() + 1}`,
            points: this.drawingPoints,
            enabled: true
        };
        
        // 添加到管理器
        const zone = this.zoneManager.addZone(newZone);
        
        if (zone) {
            console.log(`[ZoneEditor] 区域创建成功: ${zone.name}`);
            
            // 自动保存
            this._autoSave();
        }
        
        // 重置状态
        this._resetMode();
    }
    
    /**
     * 处理编辑模式下的点击
     */
    _handleEditClick(e) {
        const rect = this.canvas.getBoundingClientRect();
        const x = e.clientX - rect.left;
        const y = e.clientY - rect.top;
        const worldPos = this.radarCanvas.screenToWorld(x, y);
        
        // 检查是否点击了某个顶点
        const zone = this.zoneManager.getZone(this.editingZoneId);
        if (!zone) return;
        
        const vertexIndex = this._findNearestVertex(worldPos.x, worldPos.y, zone.points);
        
        if (vertexIndex >= 0) {
            // 开始拖拽顶点
            this.draggingVertexIndex = vertexIndex;
            this._showHint(`拖拽顶点 ${vertexIndex + 1}，右键取消`);
        }
    }
    
    /**
     * 处理顶点拖拽
     */
    _handleVertexDrag(e) {
        const rect = this.canvas.getBoundingClientRect();
        const x = e.clientX - rect.left;
        const y = e.clientY - rect.top;
        const worldPos = this.radarCanvas.screenToWorld(x, y);
        
        const zone = this.zoneManager.getZone(this.editingZoneId);
        if (!zone || this.draggingVertexIndex < 0) return;
        
        // 更新顶点位置
        zone.points[this.draggingVertexIndex] = [worldPos.x, worldPos.y];
        
        // 实时更新显示
        this.radarCanvas.setZones(this.zoneManager.getZonesArray());
    }
    
    /**
     * 查找最近的顶点
     */
    _findNearestVertex(px, py, points, threshold = 0.3) {
        let nearestIndex = -1;
        let minDistance = threshold;
        
        for (let i = 0; i < points.length; i++) {
            const [x, y] = points[i];
            const distance = Math.sqrt((x - px) ** 2 + (y - py) ** 2);
            
            if (distance < minDistance) {
                minDistance = distance;
                nearestIndex = i;
            }
        }
        
        return nearestIndex;
    }
    
    /**
     * 保存编辑后的区域
     */
    _saveEditedZone() {
        const zone = this.zoneManager.getZone(this.editingZoneId);
        if (!zone) return;
        
        // 验证区域有效性
        if (zone.points.length < 3) {
            alert('区域至少需要 3 个顶点');
            this.cancelEditing();
            return;
        }
        
        // 触发更新
        this.zoneManager.notifyUpdate();
        this._showToast('区域已更新', 'success');
    }
    
    /**
     * 开始编辑区域
     */
    startEditing(zoneId) {
        const zone = this.zoneManager.getZone(zoneId);
        if (!zone) {
            alert('区域不存在');
            return;
        }
        
        this.mode = 'edit';
        this.editingZoneId = zoneId;
        this.draggingVertexIndex = -1;
        
        // 高亮显示正在编辑的区域
        this.radarCanvas.setEditingZoneId(zoneId);
        
        // 显示工具栏
        document.getElementById('zoneToolbar').classList.remove('hidden');
        document.getElementById('btnFinishZone').style.display = 'none';
        document.getElementById('btnSaveEdit').style.display = '';
        
        // 显示提示
        this._showHint('拖拽顶点调整位置，右键或取消按钮退出');
        
        console.log(`[ZoneEditor] 开始编辑区域 ${zoneId}`);
    }
    
    /**
     * 取消编辑
     */
    cancelEditing() {
        this.mode = 'view';
        this.editingZoneId = null;
        this.draggingVertexIndex = -1;
        
        // 清除高亮
        this.radarCanvas.setEditingZoneId(null);
        
        // 隐藏提示
        this._clearHint();
        
        // 隐藏工具栏
        document.getElementById('zoneToolbar').classList.add('hidden');
        
        console.log('[ZoneEditor] 取消编辑');
    }
    
    /**
     * 删除区域
     */
    deleteZone(zoneId) {
        if (!confirm('确定要删除这个区域吗？')) {
            return;
        }
        
        const success = this.zoneManager.removeZone(zoneId);
        if (success) {
            this._showToast('区域已删除', 'success');
            console.log(`[ZoneEditor] 删除区域 ${zoneId}`);
        } else {
            this._showToast('删除失败', 'error');
        }
    }
    
    /**
     * 取消绘制
     */
    cancelDrawing() {
        console.log('[ZoneEditor] 取消绘制');
        this._resetMode();
    }
    
    /**
     * 重置模式
     */
    _resetMode() {
        this.mode = 'view';
        this.drawingPoints = [];
        
        // 隐藏工具栏
        document.getElementById('zoneToolbar').classList.add('hidden');
        
        // 清除预览
        this.radarCanvas.clearPreview();
        
        // 清除提示
        this._clearHint();
    }
    
    /**
     * 自动保存（可选，也可以手动保存）
     */
    async _autoSave() {
        try {
            const zones = this.zoneManager.exportConfig();
            const result = await this.api.saveZones(zones);
            
            if (result.success) {
                console.log('[ZoneEditor] 区域已自动保存');
            } else {
                console.warn('[ZoneEditor] 自动保存失败:', result.error);
            }
        } catch (e) {
            console.error('[ZoneEditor] 自动保存异常:', e);
        }
    }
    
    /**
     * 显示提示文字
     */
    _showHint(text) {
        // 移除旧提示
        this._clearHint();
        
        // 创建新提示
        const hint = document.createElement('div');
        hint.id = 'zoneHint';
        hint.className = 'zone-hint';
        hint.textContent = text;
        document.querySelector('.canvas-container').appendChild(hint);
    }
    
    /**
     * 清除提示
     */
    _clearHint() {
        const hint = document.getElementById('zoneHint');
        if (hint) {
            hint.remove();
        }
    }
    
    /**
     * 显示 Toast 提示
     */
    _showToast(message, type = 'info') {
        // 使用 main.js 中的 toast 系统
        if (window.App && window.App._showToast) {
            window.App._showToast(message, type);
        } else {
            // 降级方案：使用 console
            console.log(`[ZoneEditor ${type}] ${message}`);
        }
    }
    
    /**
     * 销毁
     */
    destroy() {
        // 移除工具栏
        const toolbar = document.getElementById('zoneToolbar');
        if (toolbar) {
            toolbar.remove();
        }
        
        // 清除提示
        this._clearHint();
        
        console.log('[ZoneEditor] 已销毁');
    }
}
