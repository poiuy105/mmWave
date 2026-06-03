---
name: "esp32-flash"
description: "Flash ESP32/ESP32-C3 firmware using esptool from Arduino IDE toolchain and monitor serial output. Invoke when user wants to flash ESP32 firmware, download artifacts from GitHub Actions, or monitor ESP32 serial output."
---

# ESP32 Flash Skill

This skill helps you flash ESP32/ESP32-C3 firmware using esptool from Arduino IDE toolchain, download GitHub Actions artifacts, and monitor serial output.

## When to Invoke

- User wants to flash ESP32/ESP32-C3 firmware
- User needs to download artifacts from GitHub Actions
- User wants to monitor ESP32 serial output
- User needs to find connected ESP32 devices

## Prerequisites

### 1. Arduino IDE Installation

This skill uses esptool from Arduino IDE's ESP32 package. Ensure you have:
- Arduino IDE installed
- ESP32 board package installed (via Board Manager)

### 2. esptool Location

Default path pattern:
```
%LOCALAPPDATA%\Arduino15\packages\esp32\tools\esptool_py\<version>\esptool.exe
```

Example:
```
C:\Users\<username>\AppData\Local\Arduino15\packages\esp32\tools\esptool_py\5.1.0-cn\esptool.exe
```

## Core Workflows

### Workflow 1: Find Connected ESP32 Devices

```powershell
# List all serial ports with device names
Get-PnpDevice -Class Ports | Where-Object {$_.Status -eq 'OK'} | Select-Object Name, DeviceID

# Example output:
# USB 串行设备 (COM5)  <- ESP32-C3 #1
# USB 串行设备 (COM6)  <- ESP32-C3 #2
```

### Workflow 2: Download GitHub Actions Artifacts

```powershell
# Download specific artifact to directory
gh run download <run-id> --repo <owner>/<repo> --name <artifact-name> --dir <output-dir>

# Example:
gh run download 26325858392 --repo poiuy105/rcDaulServo --name gatt_server_merged.bin --dir ./firmware
```

### Workflow 3: Flash ESP32-C3

```powershell
# Basic flash command
& '<esptool-path>' --chip esp32c3 --port <COM-port> --baud 921600 `
    --before default-reset --after hard-reset `
    write-flash -z --flash-mode dio --flash-freq 80m --flash-size 4MB `
    0x0 '<firmware-path>'
```

**Complete Example:**
```powershell
$esptool = 'C:\Users\HP\AppData\Local\Arduino15\packages\esp32\tools\esptool_py\5.1.0-cn\esptool.exe'
$firmware = 'e:\Espidf\rcDaulServo\firmware\gatt_server_merged.bin'

& $esptool --chip esp32c3 --port COM5 --baud 921600 `
    --before default-reset --after hard-reset `
    write-flash -z --flash-mode dio --flash-freq 80m --flash-size 4MB `
    0x0 $firmware
```

### Workflow 4: Monitor Serial Output

#### Method 1: PowerShell Native (Recommended)

Use Windows built-in .NET SerialPort class - no extra software needed.

```powershell
# Monitor single device
$port = New-Object System.IO.Ports.SerialPort "COM5", 115200, "None", 8, 1
$port.Open()
while ($true) {
    if ($port.BytesToRead -gt 0) {
        $data = $port.ReadExisting()
        Write-Host $data -NoNewline
    }
    Start-Sleep -Milliseconds 10
}
$port.Close()
```

#### Method 2: PowerShell with Colored Output (Multiple Devices)

```powershell
# Monitor COM5 (Owl) - Green
Start-Process powershell -ArgumentList "-NoExit","-Command","mode COM5:115200,n,8,1; Get-Content -Path COM5 -Wait -Encoding UTF8 | ForEach-Object { Write-Host '[OWL]' $_ -ForegroundColor Green }"

# Monitor COM6 (Remote) - Cyan
Start-Process powershell -ArgumentList "-NoExit","-Command","mode COM6:115200,n,8,1; Get-Content -Path COM6 -Wait -Encoding UTF8 | ForEach-Object { Write-Host '[REMOTE]' $_ -ForegroundColor Cyan }"
```

#### Method 3: PowerShell Script (Simultaneous Multi-Port)

```powershell
<#
.SYNOPSIS
    Monitor multiple ESP32 serial ports simultaneously
#>

param(
    [string[]]$Ports = @("COM5", "COM6"),
    [string[]]$Labels = @("OWL", "REMOTE"),
    [System.ConsoleColor[]]$Colors = @([System.ConsoleColor]::Green, [System.ConsoleColor]::Cyan),
    [int]$DurationSeconds = 30
)

Write-Host "=== ESP32 Serial Monitor ===" -ForegroundColor Yellow
for ($i = 0; $i -lt $Ports.Count; $i++) {
    Write-Host "$($Ports[$i]) = $($Labels[$i])" -ForegroundColor $Colors[$i]
}
Write-Host "Monitoring for $DurationSeconds seconds..." -ForegroundColor Yellow
Write-Host ""

# Configure serial ports
$serialPorts = @()
$buffers = @()

for ($i = 0; $i -lt $Ports.Count; $i++) {
    $port = New-Object System.IO.Ports.SerialPort $Ports[$i], 115200, "None", 8, 1
    $serialPorts += $port
    $buffers += ""
}

try {
    # Open all ports
    for ($i = 0; $i -lt $serialPorts.Count; $i++) {
        $serialPorts[$i].Open()
    }
    Write-Host "Ports opened, receiving data..." -ForegroundColor Green
    Write-Host ""
    
    $startTime = Get-Date
    
    while (((Get-Date) - $startTime).TotalSeconds -lt $DurationSeconds) {
        for ($i = 0; $i -lt $serialPorts.Count; $i++) {
            if ($serialPorts[$i].BytesToRead -gt 0) {
                $data = $serialPorts[$i].ReadExisting()
                $lines = ($buffers[$i] + $data) -split "`r?`n"
                $buffers[$i] = $lines[-1]
                
                for ($j = 0; $j -lt $lines.Count - 1; $j++) {
                    $line = $lines[$j].Trim()
                    if ($line -ne "") {
                        Write-Host "[$($Labels[$i])] $line" -ForegroundColor $Colors[$i]
                    }
                }
            }
        }
        Start-Sleep -Milliseconds 10
    }
    
    Write-Host ""
    Write-Host "=== Monitor End ===" -ForegroundColor Yellow
}
catch {
    Write-Host "Error: $_" -ForegroundColor Red
}
finally {
    # Close all ports
    for ($i = 0; $i -lt $serialPorts.Count; $i++) {
        if ($serialPorts[$i].IsOpen) { $serialPorts[$i].Close() }
    }
    Write-Host "Ports closed" -ForegroundColor Gray
}
```

#### Method 4: Simple Mode Command

```powershell
# Configure port
mode COM5:115200,n,8,1

# Read continuously
Get-Content -Path COM5 -Wait
```

#### Method 5: Using Arduino IDE's Monitor (if available)

```powershell
& '<arduino-ide-path>' --monitor --port COM5 --baud 115200
```

## Parameters Reference

### esptool Parameters

| Parameter | Description | Common Values |
|-----------|-------------|---------------|
| `--chip` | Target chip type | `esp32`, `esp32c3`, `esp32s3`, `esp32s2` |
| `--port` | Serial port | `COM5`, `COM6`, `/dev/ttyUSB0` |
| `--baud` | Upload baud rate | `921600`, `460800`, `115200` |
| `--before` | Action before upload | `default-reset`, `no-reset` |
| `--after` | Action after upload | `hard-reset`, `soft-reset`, `no-reset` |
| `--flash-mode` | Flash mode | `dio`, `qio`, `dout`, `qout` |
| `--flash-freq` | Flash frequency | `80m`, `40m`, `26m`, `20m` |
| `--flash-size` | Flash size | `4MB`, `2MB`, `8MB`, `16MB` |

### Serial Monitor Parameters

| Parameter | Description | Common Values |
|-----------|-------------|---------------|
| Baud rate | Serial baud rate | `115200`, `9600`, `921600` |
| Data bits | Number of data bits | `8` |
| Parity | Parity check | `None`, `Even`, `Odd` |
| Stop bits | Number of stop bits | `1`, `2` |

### Flash Address Map

| Address | Content |
|---------|---------|
| `0x0` | Bootloader + Partition Table + App (merged.bin) |
| `0x1000` | Bootloader only (if separate) |
| `0x8000` | Partition table only (if separate) |
| `0x10000` | Application only (if separate) |

## Common Scenarios

### Scenario 1: Flash Single Device

```powershell
# 1. Find device
Get-PnpDevice -Class Ports | Where-Object {$_.Status -eq 'OK'}

# 2. Download firmware (if from GitHub)
gh run download <run-id> --repo <owner>/<repo> --name <artifact> --dir ./firmware

# 3. Flash
$esptool = "$env:LOCALAPPDATA\Arduino15\packages\esp32\tools\esptool_py\5.1.0-cn\esptool.exe"
& $esptool --chip esp32c3 --port COM5 --baud 921600 `
    --before default-reset --after hard-reset `
    write-flash -z --flash-mode dio --flash-freq 80m --flash-size 4MB `
    0x0 './firmware/firmware_merged.bin'

# 4. Monitor
mode COM5:115200,n,8,1
Get-Content -Path COM5 -Wait
```

### Scenario 2: Flash Multiple Devices

```powershell
# Flash two ESP32-C3 devices
$esptool = "$env:LOCALAPPDATA\Arduino15\packages\esp32\tools\esptool_py\5.1.0-cn\esptool.exe"

# Device 1
& $esptool --chip esp32c3 --port COM5 --baud 921600 `
    --before default-reset --after hard-reset `
    write-flash -z --flash-mode dio --flash-freq 80m --flash-size 4MB `
    0x0 './firmware/gatt_server_merged.bin'

# Device 2
& $esptool --chip esp32c3 --port COM6 --baud 921600 `
    --before default-reset --after hard-reset `
    write-flash -z --flash-mode dio --flash-freq 80m --flash-size 4MB `
    0x0 './firmware/gatt_client_merged.bin'
```

### Scenario 3: Monitor Multiple Devices

```powershell
# Method 1: Separate windows
Start-Process powershell -ArgumentList "-NoExit","-Command","mode COM5:115200,n,8,1; Get-Content -Path COM5 -Wait | ForEach-Object { Write-Host '[OWL]' $_ -ForegroundColor Green }"
Start-Process powershell -ArgumentList "-NoExit","-Command","mode COM6:115200,n,8,1; Get-Content -Path COM6 -Wait | ForEach-Object { Write-Host '[REMOTE]' $_ -ForegroundColor Cyan }"

# Method 2: Single script (simultaneous)
.\monitor_serial.ps1 -Ports @("COM5", "COM6") -Labels @("OWL", "REMOTE") -DurationSeconds 60
```

### Scenario 4: Complete Flash and Monitor Workflow

```powershell
<#
.SYNOPSIS
    Complete workflow: Download, Flash, and Monitor ESP32 devices
#>

param(
    [string]$Repo = "poiuy105/rcDaulServo",
    [string]$RunId = "26325858392",
    [string[]]$Artifacts = @("gatt_server_merged.bin", "gatt_client_merged.bin"),
    [string[]]$Ports = @("COM5", "COM6"),
    [string[]]$Labels = @("OWL", "REMOTE")
)

# Configuration
$esptoolPath = "$env:LOCALAPPDATA\Arduino15\packages\esp32\tools\esptool_py\5.1.0-cn\esptool.exe"
$firmwareDir = "./firmware"

# Step 1: Download artifacts
Write-Host "=== Step 1: Downloading Firmware ===" -ForegroundColor Yellow
foreach ($artifact in $Artifacts) {
    gh run download $RunId --repo $Repo --name $artifact --dir $firmwareDir
}

# Step 2: Flash devices
Write-Host "=== Step 2: Flashing Devices ===" -ForegroundColor Yellow
for ($i = 0; $i -lt $Ports.Count; $i++) {
    $port = $Ports[$i]
    $firmware = Join-Path $firmwareDir $Artifacts[$i]
    
    Write-Host "Flashing $Labels[$i] ($firmware) to $port..." -ForegroundColor Yellow
    
    & $esptoolPath --chip esp32c3 --port $port --baud 921600 `
        --before default-reset --after hard-reset `
        write-flash -z --flash-mode dio --flash-freq 80m --flash-size 4MB `
        0x0 $firmware
}

# Step 3: Monitor
Write-Host "=== Step 3: Monitoring Serial Output ===" -ForegroundColor Yellow
Write-Host "Press Ctrl+C to stop monitoring" -ForegroundColor Gray

# Configure serial ports
$serialPorts = @()
$buffers = @()
$colors = @([System.ConsoleColor]::Green, [System.ConsoleColor]::Cyan)

for ($i = 0; $i -lt $Ports.Count; $i++) {
    $port = New-Object System.IO.Ports.SerialPort $Ports[$i], 115200, "None", 8, 1
    $serialPorts += $port
    $buffers += ""
}

try {
    for ($i = 0; $i -lt $serialPorts.Count; $i++) {
        $serialPorts[$i].Open()
    }
    Write-Host "Monitoring started..." -ForegroundColor Green
    
    while ($true) {
        for ($i = 0; $i -lt $serialPorts.Count; $i++) {
            if ($serialPorts[$i].BytesToRead -gt 0) {
                $data = $serialPorts[$i].ReadExisting()
                $lines = ($buffers[$i] + $data) -split "`r?`n"
                $buffers[$i] = $lines[-1]
                
                for ($j = 0; $j -lt $lines.Count - 1; $j++) {
                    $line = $lines[$j].Trim()
                    if ($line -ne "") {
                        Write-Host "[$($Labels[$i])] $line" -ForegroundColor $colors[$i]
                    }
                }
            }
        }
        Start-Sleep -Milliseconds 10
    }
}
finally {
    for ($i = 0; $i -lt $serialPorts.Count; $i++) {
        if ($serialPorts[$i].IsOpen) { $serialPorts[$i].Close() }
    }
}
```

## Troubleshooting

### Issue: esptool not found

**Solution:**
```powershell
# Search for esptool
Get-ChildItem -Path "$env:LOCALAPPDATA" -Recurse -Filter 'esptool*' -ErrorAction SilentlyContinue

# Or install via pip
pip install esptool
```

### Issue: Device not detected

**Solution:**
```powershell
# Check device manager
Get-PnpDevice -Class Ports

# Check if driver installed
Get-PnpDevice | Where-Object {$_.Name -like "*USB*"}

# Reinstall CP210x or CH340 driver if needed
```

### Issue: Flash fails with "Failed to connect"

**Solution:**
1. Hold BOOT button on ESP32 while flashing starts
2. Try lower baud rate: `--baud 115200`
3. Check correct COM port
4. Try `--before no-reset` option

### Issue: Permission denied on COM port

**Solution:**
```powershell
# Run PowerShell as Administrator
# Or add user to 'Users' group for serial ports
```

### Issue: Serial monitor shows garbled text

**Solution:**
```powershell
# Check baud rate matches firmware setting (usually 115200)
# Check correct encoding
$port = New-Object System.IO.Ports.SerialPort "COM5", 115200, "None", 8, 1
$port.Encoding = [System.Text.Encoding]::UTF8  # or ASCII
$port.Open()
```

### Issue: Serial port in use

**Solution:**
```powershell
# Find process using COM port
Get-Process | Where-Object {$_.ProcessName -like "*serial*" -or $_.ProcessName -like "*putty*"}

# Kill process if needed
Stop-Process -Name "processName"
```

## PowerShell Script Templates

### Template 1: Flash Only

```powershell
$esptool = "$env:LOCALAPPDATA\Arduino15\packages\esp32\tools\esptool_py\5.1.0-cn\esptool.exe"
$port = "COM5"
$firmware = "./firmware/merged.bin"

& $esptool --chip esp32c3 --port $port --baud 921600 `
    --before default-reset --after hard-reset `
    write-flash -z --flash-mode dio --flash-freq 80m --flash-size 4MB `
    0x0 $firmware
```

### Template 2: Monitor Only

```powershell
$port = New-Object System.IO.Ports.SerialPort "COM5", 115200, "None", 8, 1
$port.Open()
try {
    while ($true) {
        if ($port.BytesToRead -gt 0) {
            Write-Host $port.ReadExisting() -NoNewline
        }
        Start-Sleep -Milliseconds 10
    }
} finally {
    $port.Close()
}
```

### Template 3: Flash and Monitor

```powershell
param([string]$Port = "COM5", [string]$Firmware = "./firmware/merged.bin")

# Flash
$esptool = "$env:LOCALAPPDATA\Arduino15\packages\esp32\tools\esptool_py\5.1.0-cn\esptool.exe"
& $esptool --chip esp32c3 --port $Port --baud 921600 `
    --before default-reset --after hard-reset `
    write-flash -z --flash-mode dio --flash-freq 80m --flash-size 4MB `
    0x0 $Firmware

# Monitor
Write-Host "`nStarting monitor..." -ForegroundColor Green
$serial = New-Object System.IO.Ports.SerialPort $Port, 115200, "None", 8, 1
$serial.Open()
try {
    while ($true) {
        if ($serial.BytesToRead -gt 0) {
            Write-Host $serial.ReadExisting() -NoNewline
        }
        Start-Sleep -Milliseconds 10
    }
} finally {
    $serial.Close()
}
```

## References

- [esptool Documentation](https://docs.espressif.com/projects/esptool/)
- [ESP32 Flashing Guide](https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-guides/flash.html)
- [Arduino IDE ESP32 Package](https://github.com/espressif/arduino-esp32)
- [PowerShell SerialPort Class](https://docs.microsoft.com/en-us/dotnet/api/system.io.ports.serialport)
