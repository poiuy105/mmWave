---
name: "serial-verify-radar-protocol"
description: "通过串口调试验证雷达协议文档的正确性。当用户需要验证雷达协议MD文档、串口调试雷达数据帧、或确认雷达驱动解析逻辑时调用此skill。"
---

# 串口验证雷达协议

通过 PowerShell 串口脚本，逐项验证雷达协议文档（`*_protocol.md`）中描述的帧结构、数据编码、刷新率等功能是否与实际硬件输出一致。

## 适用范围

本项目支持以下雷达型号，每种雷达均有对应的协议文档和驱动代码：

| 雷达型号 | 协议文档 | 驱动代码 |
|----------|----------|----------|
| HLK-LD2450 | `docs/LD2450_protocol.md` | `components/radar/src/ld2450/` |
| HLK-LD2452 | `docs/LD2452_protocol.md` | `components/radar/src/ld2452/` |
| HLK-LD2460 | `docs/LD2460_protocol.md` | `components/radar/src/ld2460/` |
| HLK-LD2461 | `docs/LD2461_protocol.md` | `components/radar/src/ld2461/` |
| HLK-LD6002B | `docs/LD6002B_protocol.md` | `components/radar/src/ld6002b/` |
| HLK-LD6004 | `docs/LD6004_protocol.md` | `components/radar/src/ld6004/` |
| R60ABD1 | `docs/R60ABD1_protocol.md` | `components/radar/src/r60abd1/` |

## 前置条件

1. **硬件连接**：雷达通过 USB-TTL 模块直连电脑（推荐），或通过 ESP32 转接
2. **驱动安装**：USB-TTL 的驱动已安装（如 CP210x、CH340）
3. **无额外依赖**：使用 PowerShell 内置的 `System.IO.Ports.SerialPort` 类

## 核心工作流

### Phase 1: 协议文档分析

在编写测试脚本之前，必须先完整阅读协议文档，提取以下关键信息：

1. **通信参数**：波特率、数据位、停止位、校验位
2. **帧结构**：帧头（字节序列）、帧尾（字节序列）、帧长度（固定/可变）
3. **数据字段**：每个字段的偏移、长度、字节序（大端/小端）、数据类型
4. **编码规则**：是否有自定义编码（如 LD2452 的自定义符号位）
5. **示例数据**：协议文档中提供的完整帧示例及期望解析结果
6. **命令接口**：是否支持主机发送命令（纯上报型 vs 可配置型）
7. **性能指标**：刷新率、探测距离、方位角范围等

同时阅读对应的 C 驱动代码（`radar_*.c`），确保测试脚本的解析算法与驱动实现一致。

### Phase 2: 查找 COM 口

```powershell
Get-PnpDevice -Class Ports | Where-Object {$_.Status -eq 'OK'} | Select-Object Name, DeviceID
```

识别雷达对应的 COM 口号（如 `COM45`）。

### Phase 3: 编写验证脚本

脚本保存到 `tools/test_<型号>_protocol.ps1`，必须包含以下验证项：

#### 必选验证项

| # | 验证项 | 方法 |
|---|--------|------|
| 1 | 串口连通性 | 打开串口，等待数据流入（3 秒超时） |
| 2 | 原始数据观察 | 读取 300 字节原始数据，十六进制显示 |
| 3 | 帧头识别 | 在原始字节流中搜索帧头字节序列 |
| 4 | 帧结构验证 | 状态机解析帧头+帧尾，验证帧长度 |
| 5 | 算法自检 | 用协议文档示例数据验证解析算法 |
| 6 | 实际数据解析 | 解析真实帧数据，显示目标信息 |
| 7 | 数据编码验证 | 验证符号位、字节序等编码规则 |
| 8 | 目标有效性 | 验证空目标（全零）的判断逻辑 |
| 9 | 刷新率测试 | 统计 5 秒内帧数，计算 Hz |
| 10 | 坐标范围检查 | 验证解析值是否在协议规定的范围内 |

#### 可选验证项

| # | 验证项 | 方法 |
|---|--------|------|
| 11 | 命令发送测试 | 发送配置命令，观察是否有响应 |
| 12 | 长时间稳定性 | 持续运行 60 秒，检查丢帧率 |

### Phase 4: 脚本模板结构

```powershell
<#
.SYNOPSIS
    <雷达型号> 协议验证脚本
.PARAMETER Port
    COM 口号
.PARAMETER BaudRate
    波特率
#>
param(
    [string]$Port = "COM<默认口号>",
    [int]$BaudRate = <默认波特率>
)

# === 1. 辅助函数 ===
# Write-TestHeader, Write-Pass, Write-Fail, Write-Info
# ConvertTo-HexString（注意不要与 PowerShell 内置 Format-Hex 冲突）

# === 2. 协议常量 ===
# 从协议文档和驱动头文件中提取

# === 3. 解析函数 ===
# 必须与 C 驱动代码中的解析逻辑完全一致
# 注意 PowerShell 的 [byte] << 8 会截断为 [byte]，必须先转为 [int]

# === 4. 帧读取状态机 ===
# 复用函数 Read-Frames，参数化帧头/帧尾/帧长度

# === 5. 逐项验证 ===
# 按 Phase 3 的验证项逐一执行

# === 6. 汇总报告 ===
```

### Phase 5: 运行与结果分析

```powershell
powershell -ExecutionPolicy Bypass -File "tools/test_<型号>_protocol.ps1" -Port COM<口号>
```

分析结果时注意：

- **全部通过**：协议文档描述正确
- **部分失败**：对比实际数据与文档描述，判断是文档错误还是硬件差异
- **无数据**：检查接线、供电、波特率

## 关键注意事项

### PowerShell 串口编程陷阱

1. **`[byte] << 8` 截断问题**：PowerShell 中 `[byte]3 -shl 8` 结果为 `[byte]0`（768 截断为 0），**必须先转为 `[int]`**：
   ```powershell
   # 错误写法
   $val = $buf[0] -bor ($buf[1] -shl 8)       # $buf[1] 是 byte，-shl 8 后被截断

   # 正确写法
   $val = [int]$buf[0] -bor ([int]$buf[1] -shl 8)
   ```

2. **`Format-Hex` 函数名冲突**：PowerShell 5+ 内置 `Format-Hex` cmdlet，自定义函数会报错。改用 `ConvertTo-HexString` 等名称。

3. **UTF-8 BOM 编码**：PowerShell 5 不支持 UTF-8 无 BOM 的中文脚本。保存脚本时必须使用 UTF-8 with BOM：
   ```powershell
   $c = Get-Content $path -Raw -Encoding UTF8
   $utf8Bom = New-Object System.Text.UTF8Encoding($true)
   [System.IO.File]::WriteAllText($path, $c, $utf8Bom)
   ```

4. **`&` 字符在双引号字符串中**：PowerShell 5 中 `&` 在双引号字符串内被保留为调用运算符，可能导致解析错误。使用字符串拼接代替：
   ```powershell
   # 错误写法
   Write-Host "波特率 & 连通性"

   # 正确写法
   Write-Host ('波特率 ' + [char]0x0026 + ' 连通性')
   # 或使用单引号（不展开变量）
   Write-Host '波特率 & 连通性'
   ```

5. **`@()` 数组类型**：`@(0xAA, 0xFF)` 创建的是 `[Int32[]]`，不是 `[byte[]]`。需要 `[Array]::Copy` 到 `byte[]` 时会报错。应直接用 `New-Object byte[]` 逐字节赋值。

### 串口操作最佳实践

1. **每次测试前清空缓冲区**：`$serialPort.DiscardInBuffer()`
2. **发送命令后清空再等待**：避免残留数据干扰响应判断
3. **状态机帧同步**：与 C 驱动使用完全相同的状态机逻辑，确保验证结果可直接反映驱动正确性
4. **超时处理**：所有等待操作必须有超时，避免脚本挂起

## 验证结果判定标准

| 结果 | 含义 | 后续动作 |
|------|------|----------|
| 全部 PASS | 协议文档完全正确 | 无需修改 |
| 算法自检 FAIL | 解析算法实现有误 | 修复脚本中的解析函数 |
| 帧结构 FAIL | 帧头/帧尾/长度与实际不符 | 更新协议文档或驱动代码 |
| 刷新率异常 | 可能是固件版本差异 | 记录实际值，更新文档 |
| 坐标超范围 | 编码规则可能有误 | 检查字节序和符号位解析 |
| 命令无响应 | 确认雷达不支持命令接口 | 在文档中标注 |

## 输出文件规范

| 文件 | 路径 | 说明 |
|------|------|------|
| 测试脚本 | `tools/test_<型号>_protocol.ps1` | 验证脚本 |
| 协议文档 | `docs/<型号>_protocol.md` | 被验证的参考文档 |
| 驱动代码 | `components/radar/src/<型号>/radar_<型号>.c` | 解析算法参考 |

## 与其他 Skill 的关系

- **esp32-flash**：提供串口操作的基础方法（查找 COM 口、SerialPort 类用法）
- **esp32-radar-dev**：提供雷达驱动开发的完整流程，本 skill 专注于协议验证环节
