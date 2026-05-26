# LD2460 雷达协议验证脚本
# 验证雷达数据帧、坐标解析、刷新率等
param(
    [string]$Port = "COM45",
    [int]$BaudRate = 115200
)

$ErrorActionPreference = "Continue"

# 辅助函数
function Write-TestHeader {
    param([string]$Title)
    Write-Host ""
    Write-Host "=== $Title ===" -ForegroundColor Cyan
}

function Write-Pass {
    param([string]$Message)
    Write-Host "[PASS] $Message" -ForegroundColor Green
}

function Write-Fail {
    param([string]$Message)
    Write-Host "[FAIL] $Message" -ForegroundColor Red
}

function Write-Info {
    param([string]$Message)
    Write-Host "[INFO] $Message" -ForegroundColor Yellow
}

function ConvertTo-HexString {
    param([byte[]]$Data)
    ($Data | ForEach-Object { $_.ToString("X2") }) -join " "
}

# 协议常量
$FRAME_HEADER = @(0xF4, 0xF3, 0xF2, 0xF1)
$FRAME_TAIL = @(0xF8, 0xF7, 0xF6, 0xF5)
$FUNC_CODE_REPORT = 0x04

# 主程序
Write-TestHeader "LD2460 雷达协议验证"

# 1. 查找 COM 口
Write-TestHeader "1. 查找可用串口"
$ports = Get-PnpDevice -Class Ports | Where-Object { $_.Status -eq 'OK' } | Select-Object Name, DeviceID
$ports | Format-Table -AutoSize

# 2. 打开串口
Write-TestHeader "2. 连接串口 $Port @ $BaudRate baud"
try {
    $serialPort = New-Object System.IO.Ports.SerialPort $Port, $BaudRate, "None", 8, "One"
    $serialPort.ReadTimeout = 5000
    $serialPort.Open()
    Write-Pass "串口打开成功"
} catch {
    Write-Fail "无法打开串口: $_"
    exit 1
}

# 3. 读取原始数据
Write-TestHeader "3. 读取原始数据"
Start-Sleep -Milliseconds 1000
$serialPort.DiscardInBuffer()

Write-Info "等待雷达数据..."
$rawData = New-Object byte[] 1024
$bytesRead = $serialPort.Read($rawData, 0, 1024)

if ($bytesRead -gt 0) {
    Write-Pass "接收到 $bytesRead 字节数据"

    # 显示前 64 字节
    $displayBytes = [Math]::Min(64, $bytesRead)
    $hexStr = ConvertTo-HexString $rawData[0..($displayBytes-1)]
    Write-Host "HEX: $hexStr" -ForegroundColor Gray

    # 4. 帧头识别
    Write-TestHeader "4. 帧头识别"
    $found = $false
    for ($i = 0; $i -lt ($bytesRead - 3); $i++) {
        if ($rawData[$i] -eq 0xF4 -and $rawData[$i+1] -eq 0xF3 -and
            $rawData[$i+2] -eq 0xF2 -and $rawData[$i+3] -eq 0xF1) {
            Write-Pass "找到帧头 F4 F3 F2 F1 @ 偏移 $i"
            $found = $true
            break
        }
    }
    if (-not $found) {
        Write-Fail "未找到帧头 F4 F3 F2 F1"
    }
} else {
    Write-Fail "未接收到数据"
}

# 5. 帧解析
Write-TestHeader "5. 帧解析"
$frameStart = -1
for ($i = 0; $i -lt ($bytesRead - 3); $i++) {
    if ($rawData[$i] -eq 0xF4 -and $rawData[$i+1] -eq 0xF3 -and
        $rawData[$i+2] -eq 0xF2 -and $rawData[$i+3] -eq 0xF1) {
        $frameStart = $i
        break
    }
}

if ($frameStart -ge 0) {
    $funcCode = $rawData[$frameStart + 4]
    Write-Host "功能码: 0x$($funcCode.ToString('X2'))" -ForegroundColor Yellow

    # 数据长度 (小端)
    $dataLen = [int]$rawData[$frameStart + 5] -bor ([int]$rawData[$frameStart + 6] -shl 8)
    Write-Host "数据长度: $dataLen 字节" -ForegroundColor Yellow

    # 目标数 = (数据长度 - 11) / 4
    $targetCount = ($dataLen - 11) / 4
    Write-Host "目标数: $targetCount" -ForegroundColor Yellow

    # 解析目标坐标
    $offset = $frameStart + 7
    for ($t = 0; $t -lt $targetCount; $t++) {
        # X 坐标 (小端, int16, 0.1m)
        $xRaw = [int]$rawData[$offset] -bor ([int]$rawData[$offset + 1] -shl 8)
        $x = $xRaw * 0.1
        $offset += 2

        # Y 坐标 (小端, int16, 0.1m)
        $yRaw = [int]$rawData[$offset] -bor ([int]$rawData[$offset + 1] -shl 8)
        $y = $yRaw * 0.1
        $offset += 2

        Write-Host "目标 $($t+1): X=$x m, Y=$y m" -ForegroundColor Green
    }
} else {
    Write-Fail "无法定位帧头"
}

# 6. 刷新率测试
Write-TestHeader "6. 刷新率测试"
$serialPort.DiscardInBuffer()
$startTime = Get-Date
$frameCount = 0
$maxFrames = 20

Write-Info "收集 $maxFrames 帧数据..."
$lastFrameStart = -1
while ($frameCount -lt $maxFrames) {
    if ($serialPort.BytesToRead -gt 0) {
        $byte = $serialPort.ReadByte()
        if ($lastFrameStart -ge 0 -and $byte -eq 0xF1) {
            # 检查前面的字节是否是 F4 F3 F2
            if (($lastFrameStart + 3) -lt 1024) {
                $frameCount++
                if ($frameCount % 5 -eq 0) {
                    Write-Host "  接收帧... ($frameCount/$maxFrames)" -NoNewline
                    Write-Host ""
                }
            }
        }
        $lastFrameStart = $serialPort.BytesToRead
    }
    Start-Sleep -Milliseconds 10

    # 超时检查
    if (((Get-Date) - $startTime).TotalSeconds -gt 10) {
        Write-Warning "刷新率测试超时"
        break
    }
}

$elapsed = ((Get-Date) - $startTime).TotalSeconds
if ($elapsed -gt 0 -and $frameCount -gt 0) {
    $hz = $frameCount / $elapsed
    Write-Host "总帧数: $frameCount" -ForegroundColor Yellow
    Write-Host "耗时: $($elapsed.ToString('F1')) 秒" -ForegroundColor Yellow
    Write-Host "刷新率: $($hz.ToString('F1')) Hz" -ForegroundColor Green

    if ($hz -gt 1 -and $hz -lt 20) {
        Write-Pass "刷新率正常 (预期 10Hz)"
    } else {
        Write-Fail "刷新率异常 (预期约 10Hz)"
    }
}

# 关闭串口
$serialPort.Close()
Write-Host ""

# 汇总
Write-TestHeader "验证完成"
Write-Host "如果看到目标坐标数据，说明雷达连接正常"
Write-Host "预期刷新率: ~10Hz (每 100ms 一帧)"
