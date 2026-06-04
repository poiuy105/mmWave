# 🚀 快速启动指南

## 📋 前置条件

- 现代浏览器（Chrome/Firefox/Safari）
- ESP32-C3 后端（待开发）或模拟数据

---

## 🔧 本地测试（无后端）

### 1. 单元测试
```bash
# 直接在浏览器中打开
open test-zones.html
```

**测试内容：**
- ✅ ZoneManager 基础功能
- ✅ 点在多边形内算法
- ✅ 数据验证
- ✅ 触发状态管理

### 2. 集成测试（需要后端）
```bash
# 1. 启动 ESP32 后端
# 2. 浏览器访问
http://<esp32-ip-address>/
```

---

## 🎯 使用步骤

### 步骤 1: 绘制第一个区域

1. 打开雷达监控页面
2. 点击侧边栏 **"+ 新建区域"** 按钮
3. 在画布上点击添加顶点（至少 3 个）
4. 双击（桌面）或点击"✓ 完成"（移动端）
5. 区域自动保存

### 步骤 2: 观察触发状态

1. 当有人进入区域时，区域会变为红色高亮
2. 侧边栏显示"触发中"标签
3. WebSocket 接收实时数据

### 步骤 3: 查看配置

打开浏览器 DevTools → Console，查看日志：
```
[ZoneManager] 添加区域: 区域1 (ID: 1)
[App] 接收区域配置: 1 个
[App] 区域配置保存成功
```

---

## 🐛 调试技巧

### 查看 WebSocket 消息
```javascript
// 浏览器 Console
// 1. 打开 DevTools → Network → WS
// 2. 观察消息流

// 或手动发送测试消息
app.ws.send(JSON.stringify({
  type: "data",
  t: [[1, 2.0, 3.0, 0.5]],
  z: [1, 1]
}));
```

### 检查区域数据
```javascript
// 浏览器 Console
app.zoneManager.getZonesArray()
app.zoneManager.getStats()
```

### 手动触发测试
```javascript
// 模拟区域触发
app.zoneManager.updateTriggerStates([[1, 1]])

// 模拟取消触发
app.zoneManager.updateTriggerStates([[1, 0]])
```

---

## 📱 移动端测试

### iOS Safari
1. 确保设备在同一 WiFi
2. Safari 访问 `http://<esp32-ip>/`
3. 测试触摸绘制

### Android Chrome
1. 开启 USB 调试
2. Chrome 访问 `chrome://inspect`
3. 远程调试

---

## ⚡ 性能检查

### FPS 监控
```javascript
// 浏览器 Console
canvas._perf.fps  // 应 ≥ 50
```

### 内存监控
```javascript
// Chrome DevTools → Performance Monitor
// 观察 JS Heap Size
```

---

## 🔌 后端开发参考

### WebSocket 消息格式

#### 发送配置（后端 → 前端）
```json
{
  "type": "config",
  "zones": [
    {
      "id": 1,
      "name": "门口",
      "points": [[1.5, 3.0], [3.5, 3.0], [3.5, 5.0], [1.5, 5.0]],
      "color": "#ff6b6b",
      "enabled": true
    }
  ]
}
```

#### 发送实时数据（后端 → 前端）
```json
{
  "type": "data",
  "t": [
    [1, 1.5, 2.3, 0.5],
    [2, -0.8, 3.1, 0.2]
  ],
  "z": [1, 1]
}
```

#### 接收保存请求（前端 → 后端）
```json
{
  "type": "save_zones",
  "zones": [...]
}
```

### REST API

#### GET /api/zones
```json
{
  "zones": [...]
}
```

#### PUT /api/zones
```json
{
  "success": true
}
```

---

## 📖 相关文档

- [DEVELOPMENT_PLAN.md](DEVELOPMENT_PLAN.md) - 开发计划
- [ZONE_FEATURE_GUIDE.md](ZONE_FEATURE_GUIDE.md) - 功能使用说明
- [IMPLEMENTATION_SUMMARY.md](IMPLEMENTATION_SUMMARY.md) - 实施总结

---

## ❓ 常见问题

### Q: 绘制时没有反应？
A: 确认已点击"新建区域"按钮，查看 Console 是否有错误。

### Q: 区域没有保存？
A: 检查网络连接，确认后端 API 正常。

### Q: 触发状态不更新？
A: 检查 WebSocket 是否收到 `data` 消息。

### Q: 移动端无法绘制？
A: 使用现代浏览器，检查触摸事件支持。

---

## 🎉 开始使用！

现在你可以：
1. ✅ 运行单元测试（test-zones.html）
2. ✅ 启动后端（ESP32-C3）
3. ✅ 绘制检测区域
4. ✅ 观察触发状态

**祝使用愉快！** 🚀
