# LD Radar Web Monitor

ESP32-C3 嵌入式 Web 服务器监控系统，支持 LD 系列毫米波雷达（LD2460/LD2450/LD2452/LD2461）的可视化监控和参数配置。

## 功能特性

- **双安装模式支持**: 顶装(ceiling)和侧装(side)两种安装方式可视化
- **实时数据展示**: WebSocket 10Hz 实时推送雷达数据
- **运动轨迹显示**: 60秒轨迹记录，支持缩放和平移
- **参数配置**: 完整的雷达参数设置和保存
- **日志系统**: 系统日志记录和下载
- **多雷达适配**: 统一接口支持多种 LD 系列雷达

## 硬件要求

- ESP32-C3 开发板
- LD2460/LD2450/LD2452/LD2461 雷达模块
- 串口连接: TX/RX

## 软件架构

```
┌─────────────────────────────────────────┐
│           Web Browser                   │
│  ┌─────────┐ ┌─────────┐ ┌──────────┐  │
│  │ Canvas  │ │ WebSocket│ │ REST API │  │
│  │ 可视化  │ │ 实时数据 │ │ 配置管理 │  │
│  └─────────┘ └─────────┘ └──────────┘  │
└─────────────────────────────────────────┘
                    │
                    ▼
┌─────────────────────────────────────────┐
│         ESP32-C3 Web Server             │
│  ┌─────────┐ ┌─────────┐ ┌──────────┐  │
│  │ HTTP    │ │ WebSocket│ │ FATFS   │  │
│  │ 静态文件│ │ 服务端   │ │ 文件系统│  │
│  └─────────┘ └─────────┘ └──────────┘  │
│  ┌─────────┐ ┌─────────┐ ┌──────────┐  │
│  │ NVS     │ │ Radar   │ │ WiFi AP │  │
│  │ 配置存储│ │ 驱动    │ │ 热点    │  │
│  └─────────┘ └─────────┘ └──────────┘  │
└─────────────────────────────────────────┘
```

## 项目结构

```
├── main/
│   ├── main.c                    # 主程序入口
│   ├── CMakeLists.txt            # 主组件构建配置
│   └── web_server/
│       ├── fatfs_init.h/c        # FATFS 初始化
│       ├── http_server.h/c       # HTTP 服务器
│       ├── websocket_server.h/c  # WebSocket 服务器
│       └── ...
├── storage/
│   ├── www/                      # Web 静态文件
│   │   ├── index.html            # 主页面
│   │   ├── css/                  # 样式文件
│   │   │   ├── main.css
│   │   │   ├── layout.css
│   │   │   └── canvas.css
│   │   └── js/                   # JavaScript 文件
│   │       ├── utils.js          # 工具函数
│   │       ├── config.js         # 配置管理
│   │       ├── api.js            # REST API 客户端
│   │       ├── websocket.js      # WebSocket 客户端
│   │       ├── radar-canvas.js   # 雷达画布
│   │       └── app.js            # 主应用逻辑
│   ├── logs/                     # 日志存储
│   └── config/                   # 配置文件
├── docs/
│   └── web_monitor_design.md     # 设计文档
├── partitions.csv                # 分区表
├── CMakeLists.txt                # 项目构建配置
├── sdkconfig.defaults            # 默认配置
└── README.md                     # 本文件
```

## 构建和烧录

### 1. 环境准备

确保已安装 ESP-IDF v5.0 或更高版本：

```bash
# 检查 ESP-IDF 版本
idf.py --version
```

### 2. 配置项目

```bash
# 进入项目目录
cd /path/to/ld_radar_monitor

# 设置目标芯片
idf.py set-target esp32c3

# 配置项目（可选）
idf.py menuconfig
```

### 3. 构建项目

```bash
# 构建
idf.py build
```

### 4. 烧录固件

```bash
# 烧录（替换 /dev/ttyUSB0 为你的串口）
idf.py -p /dev/ttyUSB0 flash

# 监控输出
idf.py -p /dev/ttyUSB0 monitor
```

### 5. 一次性执行

```bash
idf.py -p /dev/ttyUSB0 flash monitor
```

## 使用方法

### 连接 WiFi

1. 上电后，ESP32-C3 会创建一个 WiFi 热点：
   - SSID: `LD-Radar-AP`
   - 密码: `12345678`

2. 用电脑或手机连接该热点

3. 打开浏览器访问: `http://192.168.4.1/`

### 网页界面

- **雷达画布**: 显示目标位置和运动轨迹
  - 鼠标滚轮: 缩放
  - 鼠标拖拽: 平移
  - 点击目标: 查看详情

- **设置面板**: 配置雷达参数
  - 雷达类型选择
  - 安装模式切换
  - 房间尺寸设置
  - 显示选项

- **日志面板**: 查看和下载系统日志

## API 接口

### REST API

| 接口 | 方法 | 说明 |
|------|------|------|
| `/api/status` | GET | 服务器状态 |
| `/api/system/info` | GET | 系统信息 |
| `/api/radar/status` | GET | 雷达状态 |
| `/api/config` | GET | 获取配置 |
| `/api/config` | PUT | 更新配置 |
| `/api/logs` | GET | 获取日志 |

### WebSocket

- 地址: `ws://192.168.4.1/ws`
- 数据格式: JSON

**订阅消息:**
```json
{
  "type": "subscribe",
  "channels": ["radar", "system"]
}
```

**雷达数据:**
```json
{
  "type": "radar_data",
  "timestamp": 1234567890,
  "frame_id": 100,
  "targets": [
    {
      "id": 1,
      "x": 1.5,
      "y": 2.0,
      "z": 1.2,
      "vx": 0.1,
      "vy": 0.2,
      "vz": 0.0,
      "snr": 35.5
    }
  ],
  "target_count": 1
}
```

## 配置说明

### 分区表 (partitions.csv)

```
# Name,   Type, SubType, Offset,  Size,      Flags
nvs,      data, nvs,     0x9000,  0x6000,         # 24KB NVS
phy_init, data, phy,     0xf000,  0x1000,         # 4KB PHY
factory,  app,  factory, 0x10000, 0x180000,       # 1.5MB App
storage,  data, fat,     ,        0x260000,       # 2.4MB FATFS
```

### 默认配置

- WiFi 模式: AP 模式
- 雷达类型: LD2460
- 波特率: 115200
- 安装模式: 侧装
- 轨迹长度: 60秒
- 数据刷新率: 10Hz

## 开发计划

- [x] Phase 1: 基础框架 (HTTP + FATFS + 前端)
- [x] Phase 2: WebSocket 实时数据
- [x] Phase 3: Canvas 可视化
- [x] Phase 4: 参数配置和 NVS 存储
- [x] Phase 5: 日志系统
- [ ] Phase 6: 多雷达适配器 (LD2450/LD2452/LD2461)
- [ ] Phase 7: 测试和优化

## 许可证

MIT License

## 作者

LD Radar Web Monitor Team
