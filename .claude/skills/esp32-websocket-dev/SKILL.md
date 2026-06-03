---
name: "esp32-websocket-dev"
description: "ESP32 WebSocket 服务器开发指南。包含从基础 API 设计到前端集成、WebSocket 握手实现的完整流程。Invoke when developing WebSocket functionality for ESP32 radar monitoring systems or need to implement real-time data streaming between ESP32 backend and web frontend."
---

# ESP32 WebSocket 开发完整指南

## 概述

本技能涵盖 ESP32-C3 雷达监控系统的 WebSocket 开发全流程，从后端 API 设计到前端集成，包含关键配置和常见问题解决方案。

## 一、后端 API 设计 (ESP-IDF)

### 1.1 HTTP Server 基础配置

**文件**: `main/web_server/http_server.c`

```c
// HTTP Server 配置 (sdkconfig.defaults)
CONFIG_HTTPD_MAX_REQ_HDR_LEN=1024
CONFIG_HTTPD_MAX_URI_LEN=512
CONFIG_HTTPD_ERR_RESP_NO_DELAY=y
CONFIG_HTTPD_PURGE_BUF_LEN=32
CONFIG_HTTPD_WS_SUPPORT=y  // 关键：启用 WebSocket 支持
```

### 1.2 静态文件路由

```c
// 显式路由注册（推荐）
httpd_uri_t static_routes[] = {
    {"/", HTTP_GET, static_file_handler, NULL},
    {"/index.html", HTTP_GET, static_file_handler, NULL},
    {"/style.css", HTTP_GET, static_file_handler, NULL},
    {"/app.js", HTTP_GET, static_file_handler, NULL},
    // ... 其他静态文件
};
```

**关键要点**:
- 使用显式路由而非 `/*` catch-all（ESP-IDF httpd 的通配符支持有限）
- `max_uri_handlers` 需要 >= 实际路由数量

### 1.3 WebSocket 服务器实现

**文件**: `main/web_server/websocket_server.c`

#### 必需头文件
```c
#include "mbedtls/sha1.h"
#include "mbedtls/base64.h"
```

#### CMakeLists.txt 依赖
```cmake
REQUIRES 
    esp_http_server
    mbedtls  // WebSocket SHA1 计算需要
```

#### WebSocket 握手关键代码
```c
// RFC 6455 Magic String
static const char WS_MAGIC[] = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";

// 计算 Sec-WebSocket-Accept
static void compute_accept_key(const char *client_key, char *accept_key, size_t len)
{
    char concat[256];
    uint8_t sha1_hash[20];
    size_t olen;
    
    // client_key + magic
    int n = snprintf(concat, sizeof(concat), "%s%s", client_key, WS_MAGIC);
    
    // SHA1
    mbedtls_sha1_context ctx;
    mbedtls_sha1_init(&ctx);
    mbedtls_sha1_starts(&ctx);
    mbedtls_sha1_update(&ctx, (const uint8_t *)concat, n);
    mbedtls_sha1_finish(&ctx, sha1_hash);
    mbedtls_sha1_free(&ctx);
    
    // Base64
    mbedtls_base64_encode((uint8_t *)accept_key, len, &olen, sha1_hash, 20);
}

// Handler 中使用
static esp_err_t ws_handler(httpd_req_t *req)
{
    char ws_key[128];
    httpd_req_get_hdr_value_str(req, "Sec-WebSocket-Key", ws_key, sizeof(ws_key));
    
    char accept_key[64];
    compute_accept_key(ws_key, accept_key, sizeof(accept_key));
    
    httpd_resp_set_status(req, "101 Switching Protocols");
    httpd_resp_set_hdr(req, "Upgrade", "websocket");
    httpd_resp_set_hdr(req, "Connection", "Upgrade");
    httpd_resp_set_hdr(req, "Sec-WebSocket-Accept", accept_key);
    
    // 获取 socket fd 并创建处理任务
    int sockfd = httpd_req_to_sockfd(req);
    // ... 创建 ws_handler_task
}
```

### 1.4 FATFS 文件上传 API

**文件**: `main/web_server/http_server.c`

```c
// 文件上传 handler
static esp_err_t api_files_upload_handler(httpd_req_t *req)
{
    // 流式接收，避免大文件 OOM
    #define UPLOAD_CHUNK_SIZE 4096
    static uint8_t chunk[UPLOAD_CHUNK_SIZE];
    
    // 分块写入文件
    while (remaining > 0) {
        int received = httpd_req_recv(req, (char *)chunk, to_recv);
        fwrite(chunk, 1, received, file);
    }
}
```

## 二、前端开发

### 2.1 WebSocket 客户端

**文件**: `frontend/websocket.js`

```javascript
class WebSocketManager {
    constructor(url) {
        this.url = url;
        this.ws = null;
        this.reconnectAttempts = 0;
        this.maxReconnectAttempts = 10;
        this.reconnectDelay = 1000;
    }

    connect() {
        this.ws = new WebSocket(this.url);
        
        this.ws.onopen = () => {
            console.log('[WS] 连接成功');
            this.reconnectAttempts = 0;
        };
        
        this.ws.onmessage = (event) => {
            const data = JSON.parse(event.data);
            this.handleMessage(data);
        };
        
        this.ws.onclose = (event) => {
            console.log(`[WS] 连接关闭: code=${event.code}`);
            this.scheduleReconnect();
        };
        
        this.ws.onerror = (error) => {
            console.error('[WS] 连接错误', error);
        };
    }

    scheduleReconnect() {
        if (this.reconnectAttempts < this.maxReconnectAttempts) {
            const delay = Math.min(1000 * Math.pow(2, this.reconnectAttempts), 30000);
            setTimeout(() => this.connect(), delay);
            this.reconnectAttempts++;
        }
    }
}
```

### 2.2 资源路径规范

**文件**: `frontend/index.html`

```html
<!-- 使用绝对路径，避免相对路径问题 -->
<link rel="stylesheet" href="/style.css">
<script src="/websocket.js"></script>
<script src="/api.js"></script>
```

**重要**: 上传到 ESP32 后，文件路径为 `/storage/www/style.css`，所以 HTML 中应使用 `/style.css` 而非 `./style.css`

### 2.3 雷达数据可视化

```javascript
// 使用 Canvas 绘制雷达点
class RadarCanvas {
    constructor(canvasId) {
        this.canvas = document.getElementById(canvasId);
        this.ctx = this.canvas.getContext('2d');
    }

    drawTarget(target) {
        const { x, y, id } = target;
        // 坐标转换：米 -> 像素
        const px = this.centerX + x * this.scale;
        const py = this.centerY - y * this.scale;
        
        this.ctx.beginPath();
        this.ctx.arc(px, py, 8, 0, Math.PI * 2);
        this.ctx.fillStyle = '#00ff00';
        this.ctx.fill();
        
        // 绘制 ID
        this.ctx.fillStyle = '#ffffff';
        this.ctx.fillText(`ID:${id}`, px + 10, py);
    }
}
```

## 三、部署流程

### 3.1 完整开发部署流程

```
1. 后端开发
   ├── 修改 main/web_server/*.c
   ├── 更新 sdkconfig.defaults（如需要）
   ├── 本地测试编译
   └── 推送到 GitHub → CI 编译

2. 前端开发
   ├── 修改 frontend/*.html, *.js, *.css
   ├── 本地浏览器测试（使用模拟数据）
   └── 推送到 GitHub

3. 部署
   ├── 下载 CI 编译的固件
   ├── 烧录到 ESP32
   ├── 通过上传页面上传 frontend 文件
   └── 验证 WebSocket 连接
```

### 3.2 烧录命令

```powershell
$esptool = "$env:LOCALAPPDATA\Arduino15\packages\esp32\tools\esptool_py\5.1.0-cn\esptool.exe"
$firmware = 'firmware_vXX\ld_radar_monitor_merged.bin'

& $esptool --chip esp32c3 --port COM19 --baud 921600 `
    --before default-reset --after hard-reset write-flash -z `
    --flash-mode dio --flash-freq 80m --flash-size 4MB 0x0 $firmware
```

### 3.3 文件上传

通过 Web 界面上传 frontend 文件：
1. 访问 `http://<ESP32_IP>/`
2. 选择 frontend 文件夹内的所有文件
3. 点击上传

## 四、常见问题与解决方案

### 4.1 WebSocket 连接失败 (code=1006)

**症状**: 浏览器显示 `WebSocket connection failed: code=1006`

**原因**: 
1. `CONFIG_HTTPD_WS_SUPPORT=y` 未启用
2. `Sec-WebSocket-Accept` 计算错误

**解决**:
```c
// sdkconfig.defaults
CONFIG_HTTPD_WS_SUPPORT=y

// websocket_server.c - 正确计算 accept key
static void compute_accept_key(const char *client_key, char *accept_key, size_t len)
{
    // SHA1(client_key + "258EAFA5-E914-47DA-95CA-C5AB0DC85B11")
    // Base64 encode
}
```

### 4.2 CSS/JS 文件 404

**症状**: 页面加载但样式和脚本丢失

**原因**:
1. 静态文件路由未正确注册
2. 前端使用相对路径 `./style.css` 而非绝对路径 `/style.css`

**解决**:
```html
<!-- 前端 index.html -->
<link rel="stylesheet" href="/style.css">  <!-- ✅ 正确 -->
<link rel="stylesheet" href="./style.css"> <!-- ❌ 错误 -->
```

### 4.3 文件上传后消失

**症状**: 上传文件成功，但重启后文件丢失

**原因**: 烧录固件时 FATFS 分区被重新格式化

**解决**: 烧录后需要重新上传文件

### 4.4 服务器崩溃重启

**症状**: 访问某些 URL 后设备重启

**原因**: 
1. WebSocket 初始化失败，`s_mutex` 为 NULL，但广播定时器仍尝试获取锁
2. `max_uri_handlers` 不足

**解决**:
```c
// 添加 NULL 检查
int websocket_get_client_count(void)
{
    if (s_mutex == NULL) return 0;
    // ...
}

// 增大 handler 数量
config.max_uri_handlers = 32;
```

## 五、关键配置清单

### sdkconfig.defaults 必需项

```
# HTTP Server
CONFIG_HTTPD_MAX_REQ_HDR_LEN=1024
CONFIG_HTTPD_MAX_URI_LEN=512
CONFIG_HTTPD_WS_SUPPORT=y  # WebSocket 必需

# FATFS
CONFIG_FATFS_LFN_HEAP=y
CONFIG_FATFS_MAX_LFN=255

# 系统
CONFIG_ESP_MAIN_TASK_STACK_SIZE=8192
```

### CMakeLists.txt 依赖

```cmake
idf_component_register(
    REQUIRES 
        esp_http_server
        mbedtls
        fatfs
        nvs_flash
        # ...
)
```

## 六、调试技巧

### 6.1 查看串口日志

```powershell
$port = New-Object System.IO.Ports.SerialPort "COM19", 115200
$port.Open()
while ($true) { 
    if ($port.BytesToRead -gt 0) { 
        Write-Host $port.ReadExisting() -NoNewline 
    } 
    Start-Sleep -Milliseconds 100 
}
```

### 6.2 测试 API

```powershell
# 检查服务器状态
Invoke-RestMethod -Uri "http://192.168.123.187/api/status"

# 检查文件列表
Invoke-RestMethod -Uri "http://192.168.123.187/api/files/list?path=/storage/www"

# 测试 WebSocket (浏览器 Console)
ws = new WebSocket('ws://192.168.123.187/ws')
ws.onmessage = (e) => console.log(JSON.parse(e.data))
```

## 七、最佳实践

1. **后端开发**: 始终使用显式路由，避免依赖 `/*` catch-all
2. **前端开发**: 使用绝对路径 `/xxx` 引用资源
3. **WebSocket**: 实现指数退避重连机制
4. **文件上传**: 使用流式接收，避免大文件 OOM
5. **错误处理**: 添加 NULL 检查，防止空指针崩溃
6. **版本管理**: 每次固件更新后重新上传 frontend 文件

## 八、参考资源

- [ESP-IDF HTTP Server 文档](https://docs.espressif.com/projects/esp-idf/en/v5.4/esp32/api-reference/protocols/esp_http_server.html)
- [RFC 6455 - WebSocket Protocol](https://tools.ietf.org/html/rfc6455)
- [WebSocket 握手详解](https://developer.mozilla.org/en-US/docs/Web/API/WebSockets_API/Writing_WebSocket_servers)
