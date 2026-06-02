# WebSocket Masked 帧问题修复记录

## 问题描述

### 症状
- Chrome浏览器WebSocket连接频繁断开重连（code=1006）
- ESP32串口日志显示大量警告：`WS frame is not properly masked`
- 广播消息失败：`Broadcast failed to fd=50: ESP_ERR_INVALID_ARG`
- 前端不断刷屏重连

### 根本原因
根据WebSocket协议（RFC 6455）：
- **客户端→服务器**：必须使用masking（掩码），Mask位=1
- **服务器→客户端**：不能使用masking，Mask位=0

ESP-IDF的`httpd_ws_frame_t`结构体包含一个`masked`字段，但在代码中所有服务器发送的帧都没有显式初始化该字段，导致未定义行为。

## 修复方案

在所有服务器发送WebSocket帧的地方，显式设置 `.masked = false`

## 修改文件清单

### 1. ws_server.c
修改了以下函数中的帧初始化：

#### ws_uri_handler() - 第43-48行
```c
httpd_ws_frame_t close_pkt = {
    .type = HTTPD_WS_TYPE_CLOSE,
    .payload = NULL,
    .len = 0,
    .masked = false,  // ← 新增
};
```

#### ws_uri_handler() - 第119-125行  
```c
httpd_ws_frame_t close_pkt = {
    .type = HTTPD_WS_TYPE_CLOSE,
    .payload = NULL,
    .len = 0,
    .masked = false,  // ← 新增
};
```

#### ws_uri_handler() - 第130-135行
```c
httpd_ws_frame_t pong_pkt = {
    .type = HTTPD_WS_TYPE_PONG,
    .payload = ws_pkt.payload,
    .len = ws_pkt.len,
    .masked = false,  // ← 新增
};
```

#### ws_server_send_text() - 第270-275行
```c
httpd_ws_frame_t ws_pkt = {
    .type = HTTPD_WS_TYPE_TEXT,
    .payload = (uint8_t *)text,
    .len = strlen(text),
    .masked = false,  // ← 新增
};
```

#### ws_server_send_binary() - 第298-303行
```c
httpd_ws_frame_t ws_pkt = {
    .type = HTTPD_WS_TYPE_BINARY,
    .payload = (uint8_t *)data,
    .len = len,
    .masked = false,  // ← 新增
};
```

#### ws_server_close_client() - 第361-366行
```c
httpd_ws_frame_t close_pkt = {
    .type = HTTPD_WS_TYPE_CLOSE,
    .payload = NULL,
    .len = 0,
    .masked = false,  // ← 新增
};
```

### 2. ws_client_mgr.c

#### ws_client_mgr_remove_timeout() - 第291-297行
```c
httpd_ws_frame_t close_pkt = {
    .type = HTTPD_WS_TYPE_CLOSE,
    .payload = NULL,
    .len = 0,
    .masked = false,  // ← 新增
};
```

#### ws_client_mgr_broadcast() - 第323-329行
```c
httpd_ws_frame_t ws_frame = {
    .type = type,
    .payload = (uint8_t *)data,
    .len = len,
    .masked = false,  // ← 新增
};
```

### 3. ws_heartbeat.c

#### ws_heartbeat_task() - 第67-73行（超时关闭）
```c
httpd_ws_frame_t close_pkt = {
    .type = HTTPD_WS_TYPE_CLOSE,
    .payload = NULL,
    .len = 0,
    .masked = false,  // ← 新增
};
```

#### ws_heartbeat_task() - 第75-82行（发送Ping）
```c
httpd_ws_frame_t ping_pkt = {
    .type = HTTPD_WS_TYPE_PING,
    .payload = NULL,
    .len = 0,
    .masked = false,  // ← 新增
};
```

#### ws_heartbeat_send_ping() - 第183-188行
```c
httpd_ws_frame_t ping_pkt = {
    .type = HTTPD_WS_TYPE_PING,
    .payload = NULL,
    .len = 0,
    .masked = false,  // ← 新增
};
```

## 验证步骤

1. **编译项目**
   ```bash
   idf.py build
   ```

2. **烧录并运行**
   ```bash
   idf.py flash monitor
   ```

3. **观察日志**
   - 不应再出现 `WS frame is not properly masked` 警告
   - WebSocket连接应该保持稳定
   - 广播消息应该成功

4. **前端测试**
   - 打开Chrome浏览器开发者工具
   - 观察Network标签中的WebSocket连接
   - 确认连接稳定，没有频繁断开重连

## 预期效果

- ✅ 消除 "WS frame is not properly masked" 警告
- ✅ WebSocket连接稳定，不再频繁断开
- ✅ 广播消息正常发送
- ✅ 前端不再刷屏重连
- ✅ 系统资源占用降低（减少无效的重连操作）

## 技术参考

- WebSocket协议规范：RFC 6455
- ESP-IDF HTTP Server WebSocket API文档
- ESP-IDF版本：需要确认具体版本对masked字段的支持情况

## 注意事项

1. **只修改服务器发送的帧**：接收帧（如ws_server.c第81行的ws_pkt）不需要设置masked字段
2. **所有发送路径都已覆盖**：包括直接发送、广播、心跳、关闭等所有场景
3. **向后兼容**：`.masked = false` 是ESP-IDF的默认行为，此修改只是显式声明，不会影响其他功能

## 后续优化建议

1. 考虑在前端增加更智能的重连退避策略
2. 添加WebSocket连接状态监控和告警
3. 优化广播性能，支持更多并发客户端
