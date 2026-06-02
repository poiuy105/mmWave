# 雷达检测区域前端开发计划

## 📋 项目概述

为 LD Radar Monitor 添加多边形检测区域功能，支持最多 8 个自定义检测区域，兼容桌面和移动端绘制。

**核心目标：**
- ✅ 可视化绘制/编辑多边形区域（最多 8 个）
- ✅ 实时显示目标与区域的交互状态
- ✅ 容错处理完善（数据验证、异常恢复）
- ✅ 移动端友好（触摸操作）
- ✅ 代码精简（适配 ESP32-C3 Flash）

---

## 🎯 功能需求

### 1. 多边形管理
- [ ] 最多支持 8 个检测区域
- [ ] 每个区域可配置：名称、颜色、启用状态
- [ ] 支持新增、编辑、删除区域
- [ ] 区域配置持久化（通过 API 保存到后端）

### 2. 绘制交互
- [ ] **桌面端**：点击添加顶点，双击完成，右键取消
- [ ] **移动端**：工具栏按钮控制（绘制/完成/取消）
- [ ] 实时预览绘制中的多边形
- [ ] 至少 3 个顶点才能完成绘制

### 3. 可视化渲染
- [ ] 已保存区域：半透明填充 + 边框 + 标签
- [ ] 触发状态：高亮闪烁动画
- [ ] 编辑中区域：虚线边框 + 顶点标记
- [ ] 目标进入区域：目标点颜色变化

### 4. 数据通信
- [ ] WebSocket 接收区域配置（`type: "config"`）
- [ ] WebSocket 接收实时数据（`type: "data"`）
- [ ] 发送保存请求（`type: "save_zones"`）
- [ ] 容错：连接断开时清空过时数据

### 5. 容错处理
- [ ] 多边形数据验证（顶点数、坐标范围）
- [ ] WebSocket 消息格式验证
- [ ] Canvas 渲染错误捕获
- [ ] 内存泄漏防护（限制区域数量、轨迹长度）

---

## 📁 文件结构

```
frontend/
├── index.html          # 修改：增加区域管理 UI
├── style.css           # 修改：增加区域相关样式
├── websocket.js        # 不变：已有完善的容错
├── api.js              # 修改：增加区域 API
├── radar.js            # 修改：增加区域触发状态处理
├── canvas.js           # 修改：增加区域渲染方法
├── zone.js             # 新增：区域数据管理
├── zone-editor.js      # 新增：区域编辑器（交互）
└── main.js             # 修改：集成区域模块
```

---

## 🚀 开发阶段

### Phase 1：基础架构（Day 1-2）

#### 任务 1.1：创建 ZoneManager 类
**文件：** `zone.js`

**功能：**
- 区域数据存储（Map 结构，最多 8 个）
- 数据验证（顶点数 3-10、坐标范围 ±20m）
- 触发状态管理
- 序列化/反序列化

**数据结构：**
```javascript
{
  id: 1,
  name: "门口",
  points: [[1.5, 3.0], [3.5, 3.0], [3.5, 5.0], [1.5, 5.0]],
  color: "#ff6b6b",
  enabled: true,
  triggered: false,
  lastTriggerTime: 0
}
```

**验收标准：**
- [ ] 能添加/删除区域
- [ ] 超过 8 个区域时拒绝添加
- [ ] 无效数据被过滤
- [ ] 单元测试通过

---

#### 任务 1.2：扩展 API 客户端
**文件：** `api.js`

**新增方法：**
```javascript
async getZones() {
  return this.request('GET', '/api/zones');
}

async saveZones(zones) {
  return this.request('PUT', '/api/zones', { zones });
}
```

**容错处理：**
- 超时重试（最多 3 次）
- 返回 null 而非抛出异常（简化前端处理）

**验收标准：**
- [ ] 能调用后端 API（模拟数据测试）
- [ ] 网络错误时返回 null
- [ ] 超时时有日志提示

---

### Phase 2：Canvas 渲染（Day 3-4）

#### 任务 2.1：扩展 RadarCanvas
**文件：** `canvas.js`

**新增方法：**
```javascript
setZones(zones)           // 设置区域数据
_drawZone(zone)           // 绘制单个区域
_drawPreviewPolygon(points) // 绘制编辑中的预览
_drawZoneVertices(zone)   // 绘制顶点（编辑模式）
```

**渲染逻辑：**
1. 在 `_render()` 中调用区域绘制
2. 根据 `triggered` 状态切换样式
3. 编辑模式显示顶点和虚线

**样式规范：**
```javascript
const zoneStyles = {
  normal: {
    fill: 'rgba(88, 166, 255, 0.1)',
    stroke: '#58a6ff',
    strokeWidth: 2
  },
  triggered: {
    fill: 'rgba(248, 81, 73, 0.25)',
    stroke: '#f85149',
    strokeWidth: 3,
    animated: true  // 闪烁
  },
  editing: {
    fill: 'rgba(255, 196, 25, 0.1)',
    stroke: '#fcc419',
    strokeWidth: 2,
    dashArray: [5, 5]
  }
};
```

**验收标准：**
- [ ] 能正确渲染多边形
- [ ] 触发状态有高亮效果
- [ ] 编辑模式显示顶点
- [ ] 性能测试：8 个区域时 FPS > 50

---

#### 任务 2.2：点在多边形内算法
**文件：** `zone.js`

**实现：**
```javascript
isPointInPolygon(point, polygon) {
  // Ray Casting 算法
  // 容错：处理边界情况、浮点数精度
}
```

**测试用例：**
- 点在内部 → true
- 点在外部 → false
- 点在边上 → true（容差 0.01m）
- 点在顶点上 → true

**验收标准：**
- [ ] 所有测试用例通过
- [ ] 性能测试：1000 次判断 < 1ms

---

### Phase 3：交互编辑（Day 5-6）

#### 任务 3.1：创建 ZoneEditor 类
**文件：** `zone-editor.js`

**功能：**
- 鼠标事件处理（点击、双击、右键）
- 触摸事件处理（pointerdown、工具栏按钮）
- 绘制状态管理
- 实时预览

**桌面端交互：**
```
1. 点击"绘制区域"按钮 → 进入绘制模式
2. 点击画布 → 添加顶点
3. 双击 → 完成绘制（≥3 顶点）
4. 右键 → 取消绘制
```

**移动端交互：**
```
1. 点击"绘制"按钮 → 进入绘制模式
2. 点击画布 → 添加顶点
3. 点击"完成"按钮 → 完成绘制
4. 点击"取消"按钮 → 取消绘制
```

**工具栏 HTML：**
```html
<div id="zoneToolbar" class="zone-toolbar hidden">
  <button id="btnFinish" class="tool-btn">✓ 完成</button>
  <button id="btnCancel" class="tool-btn">✕ 取消</button>
</div>
```

**验收标准：**
- [ ] 桌面端能正常绘制
- [ ] 移动端能正常绘制（真机测试）
- [ ] 少于 3 顶点时提示错误
- [ ] 绘制过程中有视觉反馈

---

#### 任务 3.2：区域列表 UI
**文件：** `index.html` + `style.css`

**HTML 结构：**
```html
<aside class="sidebar">
  <!-- 现有面板 -->
  
  <!-- 新增：区域管理 -->
  <div class="panel">
    <h3 class="panel-title">检测区域</h3>
    <div id="zoneList" class="zone-list">
      <!-- 动态生成 -->
    </div>
    <button id="btnAddZone" class="btn btn-primary" style="width:100%;margin-top:8px">
      + 新建区域
    </button>
  </div>
</aside>
```

**CSS 样式：**
```css
.zone-list {
  display: flex;
  flex-direction: column;
  gap: 6px;
  max-height: 200px;
  overflow-y: auto;
}

.zone-item {
  display: flex;
  align-items: center;
  gap: 8px;
  padding: 8px;
  background: var(--bg-tertiary);
  border-radius: var(--radius);
  font-size: 12px;
  cursor: pointer;
  transition: var(--transition);
}

.zone-item:hover {
  background: var(--bg-hover);
}

.zone-item.triggered {
  border: 1px solid var(--danger);
  animation: pulse-zone 1s infinite;
}

.zone-color {
  width: 12px;
  height: 12px;
  border-radius: 2px;
  flex-shrink: 0;
}

.zone-name {
  flex: 1;
  overflow: hidden;
  text-overflow: ellipsis;
  white-space: nowrap;
}

.zone-status {
  font-size: 10px;
  padding: 2px 6px;
  border-radius: 8px;
  background: var(--bg-hover);
  color: var(--text-secondary);
}

.zone-status.triggered {
  background: rgba(248, 81, 73, 0.2);
  color: var(--danger);
}

@keyframes pulse-zone {
  0%, 100% { opacity: 1; }
  50% { opacity: 0.6; }
}
```

**验收标准：**
- [ ] 区域列表正确显示
- [ ] 触发状态有动画效果
- [ ] 点击区域能选中（后续扩展）
- [ ] 超过 8 个区域时隐藏"新建"按钮

---

### Phase 4：数据流集成（Day 7-8）

#### 任务 4.1：WebSocket 消息处理
**文件：** `main.js`

**消息处理：**
```javascript
// 接收配置
this.ws.onMessage('config', (data) => {
  if (data.zones) {
    this.zoneManager.setZones(data.zones);
    this.canvas.setZones(this.zoneManager.getZones());
    this._updateZoneList();
  }
});

// 接收实时数据
this.ws.onMessage('data', (data) => {
  // 处理目标数据（已有）
  if (data.t) {
    const targets = this._parseTargets(data.t);
    this.radarData.processRadarData({ targets });
  }
  
  // 处理区域触发状态
  if (data.z) {
    this.zoneManager.updateTriggerStates(data.z);
    this.canvas.setZones(this.zoneManager.getZones());
    this._updateZoneList();
  }
});
```

**容错处理：**
- 消息格式验证（缺少字段时使用默认值）
- 异常捕获（避免崩溃）
- 日志记录（便于调试）

**验收标准：**
- [ ] 能接收并解析配置消息
- [ ] 能接收并解析实时数据
- [ ] 无效消息被忽略（有日志）
- [ ] 连接断开时清空触发状态

---

#### 任务 4.2：保存区域配置
**文件：** `main.js`

**保存流程：**
```javascript
async _saveZones() {
  const zones = this.zoneManager.getZones();
  
  // 前端验证
  if (zones.length === 0) {
    this._showToast('至少需要一个区域', 'warning');
    return;
  }
  
  try {
    const result = await this.api.saveZones(zones);
    
    if (result && result.success) {
      this._showToast('区域配置已保存', 'success');
    } else {
      this._showToast('保存失败，请重试', 'error');
    }
  } catch (e) {
    console.error('[App] 保存区域失败:', e);
    this._showToast('网络错误，请检查连接', 'error');
  }
}
```

**验收标准：**
- [ ] 点击保存按钮能调用 API
- [ ] 保存成功有提示
- [ ] 保存失败有错误提示
- [ ] 保存成功后广播给所有客户端（后端实现）

---

### Phase 5：容错与优化（Day 9-10）

#### 任务 5.1：数据验证
**文件：** `zone.js`

**验证规则：**
```javascript
_validateZone(zone) {
  // 必填字段
  if (!zone.id || !zone.name || !zone.points) return false;
  
  // 顶点数量
  if (zone.points.length < 3 || zone.points.length > 10) return false;
  
  // 坐标范围
  for (const [x, y] of zone.points) {
    if (typeof x !== 'number' || typeof y !== 'number') return false;
    if (Math.abs(x) > 20 || Math.abs(y) > 20) return false;
  }
  
  // 颜色格式
  if (zone.color && !/^#[0-9a-f]{6}$/i.test(zone.color)) return false;
  
  return true;
}
```

**验收标准：**
- [ ] 无效数据被拒绝
- [ ] 有详细的错误日志
- [ ] 不会导致程序崩溃

---

#### 任务 5.2：内存泄漏防护
**检查点：**
- [ ] 区域数量限制（最多 8 个）
- [ ] 事件监听器清理（destroy 方法）
- [ ] Canvas 动画帧清理
- [ ] WebSocket 消息处理器清理

**测试方法：**
```javascript
// 浏览器 DevTools Performance 面板
// 1. 录制 60 秒
// 2. 检查内存是否持续增长
// 3. 手动 GC 后内存应回到基线
```

---

#### 任务 5.3：性能优化
**优化项：**
1. **渲染优化**
   - 只在区域状态变化时重绘
   - 使用 requestAnimationFrame
   - 避免频繁 DOM 操作

2. **计算优化**
   - 预计算多边形包围盒
   - 先检查包围盒再精确判断

3. **网络优化**
   - WebSocket 消息节流（10Hz）
   - 压缩 JSON（去掉空格）

**性能指标：**
- FPS ≥ 50（8 个区域 + 20 个目标）
- 内存占用 < 50MB
- WebSocket 消息大小 < 2KB/帧

---

## 🧪 测试清单

### 功能测试
- [ ] 能绘制 3-10 个顶点的多边形
- [ ] 最多只能创建 8 个区域
- [ ] 第 9 个区域被拒绝并有提示
- [ ] 能删除区域
- [ ] 能编辑区域（删除重建）
- [ ] 区域配置能保存
- [ ] 刷新页面后加载已保存的区域

### 交互测试
- [ ] 桌面端：双击完成绘制
- [ ] 桌面端：右键取消绘制
- [ ] 移动端：工具栏按钮正常工作
- [ ] 触摸设备：不误触发缩放
- [ ] 绘制中有实时预览

### 可视化测试
- [ ] 区域正确渲染（填充 + 边框）
- [ ] 触发状态有高亮效果
- [ ] 触发状态有闪烁动画
- [ ] 目标进入区域时颜色变化
- [ ] 区域标签显示正确

### 容错测试
- [ ] WebSocket 断开时清空触发状态
- [ ] 无效消息被忽略
- [ ] 无效区域数据被过滤
- [ ] Canvas 渲染错误不崩溃
- [ ] 网络超时时有提示

### 性能测试
- [ ] 8 个区域 + 20 个目标时 FPS ≥ 50
- [ ] 连续运行 1 小时内存无泄漏
- [ ] WebSocket 消息大小 < 2KB/帧
- [ ] 移动端（iPhone 12）流畅运行

### 兼容性测试
- [ ] Chrome（桌面）
- [ ] Firefox（桌面）
- [ ] Safari（iOS）
- [ ] Chrome（Android）
- [ ] 微信内置浏览器（可选）

---

## 📊 验收标准

### 必须满足（P0）
1. ✅ 能绘制、保存、加载多边形区域
2. ✅ 最多支持 8 个区域
3. ✅ 桌面和移动端都能正常绘制
4. ✅ 实时显示触发状态
5. ✅ 容错处理完善（不崩溃）

### 应该满足（P1）
1. ✅ 触发状态有视觉反馈（高亮 + 动画）
2. ✅ 区域列表 UI 完整
3. ✅ 性能达标（FPS ≥ 50）
4. ✅ 内存无泄漏

### 可以满足（P2）
1. ⭕ 区域编辑（拖拽顶点）
2. ⭕ 区域复制/粘贴
3. ⭕ 导入导出配置
4. ⭕ 键盘快捷键

---

## 🛠️ 开发工具

### 调试工具
- **浏览器 DevTools**：Console、Network、Performance
- **WebSocket 测试**：wscat、Postman
- **移动端调试**：Chrome Remote Debugging、Safari Web Inspector

### 测试数据
```javascript
// 模拟 WebSocket 消息
const mockConfig = {
  type: "config",
  zones: [
    {
      id: 1,
      name: "门口",
      points: [[1.5, 3.0], [3.5, 3.0], [3.5, 5.0], [1.5, 5.0]],
      color: "#ff6b6b",
      enabled: true
    }
  ]
};

const mockData = {
  type: "data",
  t: [[1, 2.0, 4.0, 0.5]],
  z: [1, 1]  // zone_1 triggered
};
```

---

## 📅 时间规划

| 阶段 | 任务 | 预计时间 | 完成标志 |
|------|------|----------|----------|
| Day 1-2 | Phase 1：基础架构 | 2 天 | ZoneManager + API 完成 |
| Day 3-4 | Phase 2：Canvas 渲染 | 2 天 | 区域能正确渲染 |
| Day 5-6 | Phase 3：交互编辑 | 2 天 | 能绘制新区域 |
| Day 7-8 | Phase 4：数据流集成 | 2 天 | WebSocket 通信正常 |
| Day 9-10 | Phase 5：容错与优化 | 2 天 | 所有测试通过 |

**总计：10 个工作日**

---

## ⚠️ 注意事项

### 代码规范
- 使用 ES6+ 语法（箭头函数、解构赋值）
- 注释清晰（特别是几何算法）
- 变量命名语义化（避免缩写）
- 错误处理统一（try-catch + 日志）

### 移动端适配
- 触控热区 ≥ 44px × 44px
- 防止双击缩放（`touch-action: manipulation`）
- 工具栏位置避开系统导航栏
- 字体大小 ≥ 14px（可读性）

### ESP32 兼容性
- 最终代码体积 < 30KB（未压缩）
- 避免使用大型库（如 lodash）
- CSS 尽量内联到 HTML
- 图片资源不用（纯 CSS/Canvas）

---

## 🔄 后续扩展（Phase 6+）

### 后端开发（前端完成后）
1. ESP32-C3 NVS 存储接口
2. 点在多边形内算法（C 语言）
3. WebSocket 服务端（esp_http_server）
4. MQTT 上报逻辑

### 高级功能
1. 区域编辑（拖拽顶点）
2. 触发规则配置（停留时间、人数阈值）
3. 历史事件日志
4. 热力图显示

---

## 📝 变更记录

| 日期 | 版本 | 变更内容 | 作者 |
|------|------|----------|------|
| 2026-06-02 | v1.0 | 初始版本 | AI Assistant |

---

**下一步：** 开始 Phase 1 开发，创建 `zone.js` 文件。
