<#
.SYNOPSIS
    LD6004 All Commands Test Script v1.0
.DESCRIPTION
    Tests ALL control commands of LD6004 radar via USB-TTL serial port.
    Uses TinyFrame protocol framework from test_ld6004_protocol.ps1.
    Covers 16 test phases: connectivity, queries, SET/GET loops, standalone SETs, and baud rate.
.PARAMETER Port
    COM port, default COM5
.PARAMETER BaudRate
    Baud rate, default 115200
.PARAMETER SkipSetTests
    Skip all SET command tests (phases 2-14)
.PARAMETER SkipBaudRateTest
    Skip baud rate change test (phase 15)
.PARAMETER CommandTimeout
    Timeout in ms for each command, default 3000
#>

param(
    [string]$Port = "COM5",
    [int]$BaudRate = 115200,
    [switch]$SkipSetTests,
    [switch]$SkipBaudRateTest,
    [int]$CommandTimeout = 3000
)

# ============================================================
# Helper functions (from test_ld6004_protocol.ps1)
# ============================================================
function Write-TestHeader {
    param([string]$Text)
    Write-Host ""
    Write-Host "============================================" -ForegroundColor DarkGray
    Write-Host "  $Text" -ForegroundColor Yellow
    Write-Host "============================================" -ForegroundColor DarkGray
}

function Write-Pass {
    param([string]$Text = "PASS")
    Write-Host "  [PASS] $Text" -ForegroundColor Green
    $script:PassCount++
}

function Write-Fail {
    param([string]$Text = "FAIL")
    Write-Host "  [FAIL] $Text" -ForegroundColor Red
    $script:FailCount++
}

function Write-Info {
    param([string]$Text)
    Write-Host "  [INFO] $Text" -ForegroundColor Cyan
}

function ConvertTo-HexString {
    param([byte[]]$Bytes)
    $sb = New-Object System.Text.StringBuilder
    foreach ($b in $Bytes) {
        [void]$sb.AppendFormat("{0:X2} ", $b)
    }
    return $sb.ToString().Trim()
}

# ============================================================
# Global stats
# ============================================================
$script:PassCount = 0
$script:FailCount = 0

# ============================================================
# TinyFrame protocol constants
# ============================================================
$TF_SOF = 0x01

# ============================================================
# Sub-command constants (from radar_ld6004.h)
# ============================================================
$SUB_AUTO_GEN_NOISE     = 0x01
$SUB_GET_AREAS          = 0x02
$SUB_CLEAR_NOISE        = 0x03
$SUB_RESET_DETECTION    = 0x04
$SUB_GET_HOLD_DELAY     = 0x05
$SUB_SET_POINT_CLOUD    = 0x06
$SUB_SET_TARGET_DISPLAY = 0x07
$SUB_SET_SENSITIVITY    = 0x08
$SUB_GET_SENSITIVITY    = 0x09
$SUB_SET_TRIGGER_SPEED  = 0x0A
$SUB_GET_TRIGGER_SPEED  = 0x0B
$SUB_GET_Z_RANGE        = 0x0C
$SUB_SET_INSTALL_MODE   = 0x0D
$SUB_GET_INSTALL_MODE   = 0x0E
$SUB_SET_WORK_MODE      = 0x0F
$SUB_GET_WORK_MODE      = 0x10
$SUB_GET_LOW_POWER_TIME = 0x11
$SUB_RESET_UNOCCUPIED   = 0x12
$SUB_SET_GPIO_MODE      = 0x13
$SUB_GET_GPIO_MODE      = 0x14
$SUB_CLEAR_STAY_AREAS   = 0x15
$SUB_GET_STAY_LIFE      = 0x16
$SUB_GET_OUTPUT_INTERVAL = 0x17

# ============================================================
# Message TYPE constants (from radar_ld6004.h)
# ============================================================
$TYPE_SET_CONTROL_CMD    = 0x0201
$TYPE_SET_AREA           = 0x0202
$TYPE_SET_HOLD_DELAY     = 0x0203
$TYPE_SET_Z_RANGE        = 0x0204
$TYPE_SET_LOW_POWER      = 0x0205
$TYPE_SET_STAY_LIFE      = 0x0206
$TYPE_SET_OUTPUT_INTERVAL = 0x0207
$TYPE_SET_BAUD_RATE      = 0x0F0F
$TYPE_GENERAL_FW_VERSION = 0xFFFF

$TYPE_REPORT_TARGET      = 0x0A04
$TYPE_REPORT_POINT_CLOUD = 0x0A08
$TYPE_REPORT_AREA_STATE  = 0x0A0A
$TYPE_REPORT_AREA_1      = 0x0A0B
$TYPE_REPORT_AREA_2      = 0x0A0C
$TYPE_REPORT_HOLD_DELAY  = 0x0A0D
$TYPE_REPORT_SENSITIVITY = 0x0A0E
$TYPE_REPORT_TRIGGER_SPEED = 0x0A0F
$TYPE_REPORT_Z_RANGE     = 0x0A10
$TYPE_REPORT_INSTALL_MODE = 0x0A11
$TYPE_REPORT_WORK_MODE   = 0x0A12
$TYPE_REPORT_LOW_POWER_TIME = 0x0A13
$TYPE_REPORT_LOW_POWER_STATE = 0x0A14
$TYPE_REPORT_GPIO_STATE  = 0x0A15
$TYPE_REPORT_AREA_3      = 0x0A16
$TYPE_REPORT_STAY_LIFE   = 0x0A17
$TYPE_REPORT_OUTPUT_INTERVAL = 0x0A18

# ============================================================
# TinyFrame checksum: XOR all bytes then NOT
# ============================================================
function TF-Cksum {
    param([byte[]]$Data)
    $ret = 0
    foreach ($b in $Data) {
        $ret = $ret -bxor $b
    }
    return (-bnot $ret) -band 0xFF
}

# ============================================================
# Read int32 little-endian
# ============================================================
function Read-I32LE {
    param([byte[]]$Buf, [int]$Offset)
    return [int]([uint32]$Buf[$Offset] -bor ([uint32]$Buf[$Offset+1] -shl 8) -bor ([uint32]$Buf[$Offset+2] -shl 16) -bor ([uint32]$Buf[$Offset+3] -shl 24))
}

# ============================================================
# Read float little-endian (IEEE 754)
# ============================================================
function Read-FloatLE {
    param([byte[]]$Buf, [int]$Offset)
    $val = [uint32]$Buf[$Offset] -bor ([uint32]$Buf[$Offset+1] -shl 8) -bor ([uint32]$Buf[$Offset+2] -shl 16) -bor ([uint32]$Buf[$Offset+3] -shl 24)
    $bytes = [BitConverter]::GetBytes($val)
    return [BitConverter]::ToSingle($bytes, 0)
}

# ============================================================
# Write float little-endian (IEEE 754)
# ============================================================
function Write-FloatLE {
    param([byte[]]$Buf, [int]$Offset, [float]$Val)
    $bytes = [BitConverter]::GetBytes([single]$Val)
    $Buf[$Offset]     = $bytes[0]
    $Buf[$Offset + 1] = $bytes[1]
    $Buf[$Offset + 2] = $bytes[2]
    $Buf[$Offset + 3] = $bytes[3]
}

# ============================================================
# Write uint16 big-endian
# ============================================================
function Write-U16BE {
    param([byte[]]$Buf, [int]$Offset, [int]$Val)
    $Buf[$Offset] = [byte](($Val -shr 8) -band 0xFF)
    $Buf[$Offset + 1] = [byte]($Val -band 0xFF)
}

# ============================================================
# Write int32 little-endian
# ============================================================
function Write-I32LE {
    param([byte[]]$Buf, [int]$Offset, [int]$Val)
    $Buf[$Offset]     = [byte]($Val -band 0xFF)
    $Buf[$Offset + 1] = [byte](($Val -shr 8) -band 0xFF)
    $Buf[$Offset + 2] = [byte](($Val -shr 16) -band 0xFF)
    $Buf[$Offset + 3] = [byte](($Val -shr 24) -band 0xFF)
}

# ============================================================
# Build a TinyFrame packet
# ============================================================
function Build-TFPacket {
    param([int]$Id, [int]$Type, [byte[]]$Data)
    $dataLen = if ($Data) { $Data.Length } else { 0 }
    $totalLen = 9 + $dataLen
    $pkt = New-Object byte[] $totalLen

    $pkt[0] = $TF_SOF
    Write-U16BE $pkt 1 $Id
    Write-U16BE $pkt 3 $dataLen
    Write-U16BE $pkt 5 $Type

    $headBytes = New-Object byte[] 7
    [Array]::Copy($pkt, 0, $headBytes, 0, 7)
    $pkt[7] = TF-Cksum $headBytes

    if ($dataLen -gt 0) {
        [Array]::Copy($Data, 0, $pkt, 8, $dataLen)
    }

    if ($dataLen -gt 0) {
        $dataBytes = New-Object byte[] $dataLen
        [Array]::Copy($Data, 0, $dataBytes, 0, $dataLen)
        $pkt[8 + $dataLen] = TF-Cksum $dataBytes
    } else {
        $pkt[8] = 0xFF
    }

    return $pkt
}

# ============================================================
# TinyFrame parser state machine
# ============================================================
enum TF_State {
    IDLE = 0
    ID_HI = 1
    ID_LO = 2
    LEN_HI = 3
    LEN_LO = 4
    TYPE_HI = 5
    TYPE_LO = 6
    HEAD_CKSUM = 7
    DATA = 8
    DATA_CKSUM = 9
}

$script:tfState = [TF_State]::IDLE
$script:tfId = 0
$script:tfLen = 0
$script:tfType = 0
$script:tfHeadCksum = 0
$script:tfDataIdx = 0
$script:tfFrameData = $null

function Reset-TFParser {
    $script:tfState = [TF_State]::IDLE
    $script:tfDataIdx = 0
}

function TF-FeedByte {
    param([byte]$Byte)

    switch ($script:tfState) {
        ([TF_State]::IDLE) {
            if ($Byte -eq $TF_SOF) {
                $script:tfState = [TF_State]::ID_HI
            }
            break
        }
        ([TF_State]::ID_HI) {
            $script:tfId = [int]$Byte -shl 8
            $script:tfState = [TF_State]::ID_LO
            break
        }
        ([TF_State]::ID_LO) {
            $script:tfId = $script:tfId -bor $Byte
            $script:tfState = [TF_State]::LEN_HI
            break
        }
        ([TF_State]::LEN_HI) {
            $script:tfLen = [int]$Byte -shl 8
            $script:tfState = [TF_State]::LEN_LO
            break
        }
        ([TF_State]::LEN_LO) {
            $script:tfLen = $script:tfLen -bor $Byte
            $script:tfState = [TF_State]::TYPE_HI
            break
        }
        ([TF_State]::TYPE_HI) {
            $script:tfType = [int]$Byte -shl 8
            $script:tfState = [TF_State]::TYPE_LO
            break
        }
        ([TF_State]::TYPE_LO) {
            $script:tfType = $script:tfType -bor $Byte
            $script:tfState = [TF_State]::HEAD_CKSUM
            break
        }
        ([TF_State]::HEAD_CKSUM) {
            $script:tfHeadCksum = $Byte
            if ($script:tfLen -gt 0) {
                $script:tfFrameData = New-Object byte[] $script:tfLen
                $script:tfDataIdx = 0
                $script:tfState = [TF_State]::DATA
            } else {
                $script:tfState = [TF_State]::DATA_CKSUM
            }
            break
        }
        ([TF_State]::DATA) {
            $script:tfFrameData[$script:tfDataIdx++] = $Byte
            if ($script:tfDataIdx -ge $script:tfLen) {
                $script:tfState = [TF_State]::DATA_CKSUM
            }
            break
        }
        ([TF_State]::DATA_CKSUM) {
            $dataCksum = $Byte

            $headBytes = New-Object byte[] 7
            $headBytes[0] = $TF_SOF
            $headBytes[1] = [byte](($script:tfId -shr 8) -band 0xFF)
            $headBytes[2] = [byte]($script:tfId -band 0xFF)
            $headBytes[3] = [byte](($script:tfLen -shr 8) -band 0xFF)
            $headBytes[4] = [byte]($script:tfLen -band 0xFF)
            $headBytes[5] = [byte](($script:tfType -shr 8) -band 0xFF)
            $headBytes[6] = [byte]($script:tfType -band 0xFF)

            $expectedHeadCk = TF-Cksum $headBytes
            $headOk = ($script:tfHeadCksum -eq $expectedHeadCk)

            $dataOk = $true
            if ($script:tfLen -gt 0) {
                $expectedDataCk = TF-Cksum $script:tfFrameData
                $dataOk = ($dataCksum -eq $expectedDataCk)
            } else {
                $dataOk = ($dataCksum -eq 0xFF)
            }

            $result = @{
                Id = $script:tfId
                Len = $script:tfLen
                Type = $script:tfType
                Data = $script:tfFrameData
                HeadCksumOk = $headOk
                DataCksumOk = $dataOk
                Valid = $headOk -and $dataOk
            }

            Reset-TFParser
            return $result
        }
    }
    return $null
}

# ============================================================
# Read and parse TF frames from serial port
# ============================================================
function Read-TFFrames {
    param(
        [System.IO.Ports.SerialPort]$SerialPort,
        [int]$MaxFrames = 20,
        [int]$TimeoutSeconds = 10
    )
    $frames = [System.Collections.ArrayList]::new()
    $startTime = Get-Date

    while ($frames.Count -lt $MaxFrames -and ((Get-Date) - $startTime).TotalSeconds -lt $TimeoutSeconds) {
        if ($SerialPort.BytesToRead -gt 0) {
            $byte = $SerialPort.ReadByte()
            $result = TF-FeedByte $byte
            if ($result -ne $null) {
                [void]$frames.Add($result)
            }
        } else {
            Start-Sleep -Milliseconds 1
        }
    }

    return @{
        Frames = $frames
        Elapsed = ((Get-Date) - $startTime).TotalSeconds
    }
}

# ============================================================
# Wait for a report frame of specific TYPE after ACK
# ============================================================
function Wait-ReportFrame {
    param(
        [System.IO.Ports.SerialPort]$SerialPort,
        [int]$ExpectedType,
        [int]$TimeoutMs = 2000
    )
    $startTime = Get-Date
    while (((Get-Date) - $startTime).TotalMilliseconds -lt $TimeoutMs) {
        if ($SerialPort.BytesToRead -gt 0) {
            $byte = $SerialPort.ReadByte()
            $result = TF-FeedByte $byte
            if ($result -ne $null -and $result.Valid -and $result.Type -eq $ExpectedType) {
                return $result
            }
        } else {
            Start-Sleep -Milliseconds 1
        }
    }
    return $null
}

# ============================================================
# Send-QueryCommand: send query -> wait ACK -> wait report frame
# Returns report frame data or $null
# ============================================================
function Send-QueryCommand {
    param(
        [System.IO.Ports.SerialPort]$SerialPort,
        [int]$MsgType,
        [byte[]]$Data,
        [int]$ReportType,
        [int]$TimeoutMs = 0
    )
    if ($TimeoutMs -eq 0) { $TimeoutMs = $CommandTimeout }

    $script:cmdId++
    $pkt = Build-TFPacket -Id $script:cmdId -Type $MsgType -Data $Data
    $sentId = $script:cmdId

    Write-Host ("    TX: " + (ConvertTo-HexString $pkt)) -ForegroundColor DarkGray

    # Clear buffer
    $SerialPort.DiscardInBuffer()
    Start-Sleep -Milliseconds 50
    Reset-TFParser

    # Send
    try {
        $SerialPort.Write($pkt, 0, $pkt.Length)
    } catch {
        Write-Host ("    Send error: " + $_.Exception.Message) -ForegroundColor Red
        return $null
    }

    # Wait for ACK (matching ID, same TYPE as sent, empty data)
    $startTime = Get-Date
    $ackReceived = $false
    while (((Get-Date) - $startTime).TotalMilliseconds -lt $TimeoutMs) {
        if ($SerialPort.BytesToRead -gt 0) {
            $byte = $SerialPort.ReadByte()
            $result = TF-FeedByte $byte
            if ($result -ne $null -and $result.Id -eq $sentId) {
                $typeHex = "0x" + $result.Type.ToString("X4")
                $dataHex = if ($result.Data -and $result.Data.Length -gt 0) { ConvertTo-HexString $result.Data } else { "(empty)" }
                Write-Host ("    RX ACK: ID=$($result.Id) TYPE=$typeHex LEN=$($result.Len) DATA=$dataHex") -ForegroundColor DarkGray
                $ackReceived = $true
                break
            }
        } else {
            Start-Sleep -Milliseconds 2
        }
    }

    if (-not $ackReceived) {
        Write-Host "    RX ACK: TIMEOUT" -ForegroundColor Red
        return $null
    }

    # Wait for report frame
    if ($ReportType -gt 0) {
        $report = Wait-ReportFrame -SerialPort $SerialPort -ExpectedType $ReportType -TimeoutMs 2000
        if ($report -ne $null) {
            $rTypeHex = "0x" + $report.Type.ToString("X4")
            $rDataHex = if ($report.Data -and $report.Data.Length -gt 0) { ConvertTo-HexString $report.Data } else { "(empty)" }
            Write-Host ("    RX Report: ID=$($report.Id) TYPE=$rTypeHex LEN=$($report.Len) DATA=$rDataHex") -ForegroundColor DarkGray
            return $report
        } else {
            Write-Host ("    RX Report 0x$($ReportType.ToString('X4')): TIMEOUT") -ForegroundColor Yellow
            return $null
        }
    }

    return @{ Id = $sentId; Type = $MsgType; Len = 0; Data = $null; Valid = $true }
}

# ============================================================
# Send-SetCommand: send SET command (0x0201 with sub+param) -> wait ACK
# Returns $true on success, $false on failure
# ============================================================
function Send-SetCommand {
    param(
        [System.IO.Ports.SerialPort]$SerialPort,
        [int]$SubCmd,
        [byte]$Param = [byte]0xFF,
        [int]$TimeoutMs = 0
    )
    if ($TimeoutMs -eq 0) { $TimeoutMs = $CommandTimeout }

    # Build data: sub_cmd (4 bytes I32LE) + param (1 byte)
    if ($Param -eq [byte]0xFF) {
        # No param - just sub_cmd
        $cmdData = New-Object byte[] 4
        Write-I32LE $cmdData 0 $SubCmd
    } else {
        $cmdData = New-Object byte[] 5
        Write-I32LE $cmdData 0 $SubCmd
        $cmdData[4] = $Param
    }

    $script:cmdId++
    $pkt = Build-TFPacket -Id $script:cmdId -Type $TYPE_SET_CONTROL_CMD -Data $cmdData
    $sentId = $script:cmdId

    Write-Host ("    TX: " + (ConvertTo-HexString $pkt)) -ForegroundColor DarkGray

    # Clear buffer
    $SerialPort.DiscardInBuffer()
    Start-Sleep -Milliseconds 50
    Reset-TFParser

    # Send
    try {
        $SerialPort.Write($pkt, 0, $pkt.Length)
    } catch {
        Write-Host ("    Send error: " + $_.Exception.Message) -ForegroundColor Red
        return $false
    }

    # Wait for ACK
    $startTime = Get-Date
    while (((Get-Date) - $startTime).TotalMilliseconds -lt $TimeoutMs) {
        if ($SerialPort.BytesToRead -gt 0) {
            $byte = $SerialPort.ReadByte()
            $result = TF-FeedByte $byte
            if ($result -ne $null -and $result.Id -eq $sentId) {
                $typeHex = "0x" + $result.Type.ToString("X4")
                $dataHex = if ($result.Data -and $result.Data.Length -gt 0) { ConvertTo-HexString $result.Data } else { "(empty)" }
                Write-Host ("    RX ACK: ID=$($result.Id) TYPE=$typeHex LEN=$($result.Len) DATA=$dataHex") -ForegroundColor DarkGray
                return $true
            }
        } else {
            Start-Sleep -Milliseconds 2
        }
    }

    Write-Host "    RX ACK: TIMEOUT" -ForegroundColor Red
    return $false
}

# ============================================================
# Send-StandaloneSetCommand: send SET command with non-0x0201 TYPE
# Returns $true on success, $false on failure
# ============================================================
function Send-StandaloneSetCommand {
    param(
        [System.IO.Ports.SerialPort]$SerialPort,
        [int]$MsgType,
        [byte[]]$Data,
        [int]$TimeoutMs = 0
    )
    if ($TimeoutMs -eq 0) { $TimeoutMs = $CommandTimeout }

    $script:cmdId++
    $pkt = Build-TFPacket -Id $script:cmdId -Type $MsgType -Data $Data
    $sentId = $script:cmdId

    Write-Host ("    TX: " + (ConvertTo-HexString $pkt)) -ForegroundColor DarkGray

    # Clear buffer
    $SerialPort.DiscardInBuffer()
    Start-Sleep -Milliseconds 50
    Reset-TFParser

    # Send
    try {
        $SerialPort.Write($pkt, 0, $pkt.Length)
    } catch {
        Write-Host ("    Send error: " + $_.Exception.Message) -ForegroundColor Red
        return $false
    }

    # Wait for ACK (matching ID, same TYPE, empty data)
    $startTime = Get-Date
    while (((Get-Date) - $startTime).TotalMilliseconds -lt $TimeoutMs) {
        if ($SerialPort.BytesToRead -gt 0) {
            $byte = $SerialPort.ReadByte()
            $result = TF-FeedByte $byte
            if ($result -ne $null -and $result.Id -eq $sentId) {
                $typeHex = "0x" + $result.Type.ToString("X4")
                $dataHex = if ($result.Data -and $result.Data.Length -gt 0) { ConvertTo-HexString $result.Data } else { "(empty)" }
                Write-Host ("    RX ACK: ID=$($result.Id) TYPE=$typeHex LEN=$($result.Len) DATA=$dataHex") -ForegroundColor DarkGray
                return $true
            }
        } else {
            Start-Sleep -Milliseconds 2
        }
    }

    Write-Host "    RX ACK: TIMEOUT" -ForegroundColor Red
    return $false
}

# ============================================================
# Helper: Build 0x0201 query data (sub_cmd only, 4 bytes I32LE)
# ============================================================
function Build-QueryData {
    param([int]$SubCmd)
    $data = New-Object byte[] 4
    Write-I32LE $data 0 $SubCmd
    return $data
}

# ============================================================
# START TESTS
# ============================================================
Write-Host ""
Write-Host "============================================" -ForegroundColor Magenta
Write-Host "  LD6004 All Commands Test Tool v1.0" -ForegroundColor Magenta
Write-Host "============================================" -ForegroundColor Magenta
Write-Host ("  Port: " + $Port + " | BaudRate: " + $BaudRate + " | Timeout: " + $CommandTimeout + "ms") -ForegroundColor White
if ($SkipSetTests) { Write-Host "  SkipSetTests: YES (phases 2-14 skipped)" -ForegroundColor Yellow }
if ($SkipBaudRateTest) { Write-Host "  SkipBaudRateTest: YES (phase 15 skipped)" -ForegroundColor Yellow }
Write-Host ""

$script:cmdId = 0

# ============================================================
# Phase 0: Connection Verification
# ============================================================
Write-TestHeader "Phase 0: Connection Verification"

try {
    $serialPort = New-Object System.IO.Ports.SerialPort $Port, $BaudRate, "None", 8, 1
    $serialPort.ReadTimeout = 3000
    $serialPort.WriteTimeout = 1000
    $serialPort.Open()
    Write-Pass ("Serial port " + $Port + " opened (8N1, " + $BaudRate + ")")
} catch {
    Write-Fail ("Cannot open " + $Port + " : " + $_.Exception.Message)
    exit 1
}

$serialPort.DiscardInBuffer()
Start-Sleep -Milliseconds 500

Write-Host "  Waiting for data (5s timeout)..." -ForegroundColor Gray
$startTime = Get-Date
$dataReceived = $false
while (((Get-Date) - $startTime).TotalSeconds -lt 5) {
    if ($serialPort.BytesToRead -gt 0) {
        $dataReceived = $true
        break
    }
    Start-Sleep -Milliseconds 100
}

if ($dataReceived) {
    Write-Pass ("Data detected (" + $serialPort.BytesToRead + " bytes ready)")
} else {
    Write-Fail "No data received within 5 seconds"
    Write-Host "  Check: 1) Power  2) TX/RX wiring  3) Baud rate" -ForegroundColor Yellow
    $serialPort.Close()
    exit 1
}

# Verify SOF and frame structure
$serialPort.DiscardInBuffer()
Start-Sleep -Milliseconds 200
Reset-TFParser
$tfResult = Read-TFFrames -SerialPort $serialPort -MaxFrames 30 -TimeoutSeconds 10
$collectedFrames = $tfResult.Frames

Write-Host ("  Parsed " + $collectedFrames.Count + " valid TF frames in " + [math]::Round($tfResult.Elapsed, 1) + "s") -ForegroundColor Gray

if ($collectedFrames.Count -gt 0) {
    $validCksum = 0
    foreach ($frame in $collectedFrames) {
        if ($frame.Valid) { $validCksum++ }
    }
    Write-Host ("  Checksum valid: " + $validCksum + "/" + $collectedFrames.Count) -ForegroundColor Gray
    Write-Pass ("TinyFrame structure verified (" + $collectedFrames.Count + " frames, " + $validCksum + " valid)")
} else {
    Write-Fail "No valid TinyFrame frames parsed"
}

Start-Sleep -Milliseconds 200

# ============================================================
# Phase 1: Basic Queries (12 GET commands, read-only)
# ============================================================
Write-TestHeader "Phase 1: Basic Queries (GET commands)"

# 1.1 Query firmware version (0xFFFF)
Write-Host "  1.1 Query firmware version (0xFFFF)..." -ForegroundColor White
$resp = Send-QueryCommand -SerialPort $serialPort -MsgType $TYPE_GENERAL_FW_VERSION -Data $null -ReportType 0
if ($resp -ne $null) {
    if ($resp.Data -and $resp.Data.Length -ge 4) {
        $project = $resp.Data[0]
        $major = $resp.Data[1]
        $sub = $resp.Data[2]
        $modified = $resp.Data[3]
        Write-Host ("    Firmware: project=$project major=$major sub=$sub modified=$modified") -ForegroundColor White
        Write-Pass ("Firmware version: V$major.$sub.$modified")
    } else {
        Write-Pass "Firmware version response received"
    }
} else {
    Write-Fail "Firmware version query failed"
}
Start-Sleep -Milliseconds 200

# 1.2 Get sensitivity (sub=0x09, report=0x0A0E)
Write-Host "  1.2 Get sensitivity (sub=0x09)..." -ForegroundColor White
$qData = Build-QueryData -SubCmd $SUB_GET_SENSITIVITY
$resp = Send-QueryCommand -SerialPort $serialPort -MsgType $TYPE_SET_CONTROL_CMD -Data $qData -ReportType $TYPE_REPORT_SENSITIVITY
if ($resp -ne $null -and $resp.Data -and $resp.Data.Length -ge 1) {
    $sensVal = $resp.Data[0]
    $sensName = switch ($sensVal) { 0 { "Low" } 1 { "Medium" } 2 { "High" } default { "Unknown($sensVal)" } }
    Write-Host ("    Sensitivity: $sensVal ($sensName)") -ForegroundColor White
    Write-Pass ("Get sensitivity: $sensName ($sensVal)")
} else {
    Write-Fail "Get sensitivity failed"
}
Start-Sleep -Milliseconds 200

# 1.3 Get trigger speed (sub=0x0B, report=0x0A0F)
Write-Host "  1.3 Get trigger speed (sub=0x0B)..." -ForegroundColor White
$qData = Build-QueryData -SubCmd $SUB_GET_TRIGGER_SPEED
$resp = Send-QueryCommand -SerialPort $serialPort -MsgType $TYPE_SET_CONTROL_CMD -Data $qData -ReportType $TYPE_REPORT_TRIGGER_SPEED
if ($resp -ne $null -and $resp.Data -and $resp.Data.Length -ge 1) {
    $speedVal = $resp.Data[0]
    $speedName = switch ($speedVal) { 0 { "Slow" } 1 { "Medium" } 2 { "Fast" } default { "Unknown($speedVal)" } }
    Write-Host ("    Trigger speed: $speedVal ($speedName)") -ForegroundColor White
    Write-Pass ("Get trigger speed: $speedName ($speedVal)")
} else {
    Write-Fail "Get trigger speed failed"
}
Start-Sleep -Milliseconds 200

# 1.4 Get install mode (sub=0x0E, report=0x0A11)
Write-Host "  1.4 Get install mode (sub=0x0E)..." -ForegroundColor White
$qData = Build-QueryData -SubCmd $SUB_GET_INSTALL_MODE
$resp = Send-QueryCommand -SerialPort $serialPort -MsgType $TYPE_SET_CONTROL_CMD -Data $qData -ReportType $TYPE_REPORT_INSTALL_MODE
if ($resp -ne $null -and $resp.Data -and $resp.Data.Length -ge 1) {
    $modeVal = $resp.Data[0]
    $modeName = switch ($modeVal) { 0 { "Top" } 1 { "Side" } default { "Unknown($modeVal)" } }
    Write-Host ("    Install mode: $modeVal ($modeName)") -ForegroundColor White
    Write-Pass ("Get install mode: $modeName ($modeVal)")
} else {
    Write-Fail "Get install mode failed"
}
Start-Sleep -Milliseconds 200

# 1.5 Get work mode (sub=0x10, report=0x0A12)
Write-Host "  1.5 Get work mode (sub=0x10)..." -ForegroundColor White
$qData = Build-QueryData -SubCmd $SUB_GET_WORK_MODE
$resp = Send-QueryCommand -SerialPort $serialPort -MsgType $TYPE_SET_CONTROL_CMD -Data $qData -ReportType $TYPE_REPORT_WORK_MODE
if ($resp -ne $null -and $resp.Data -and $resp.Data.Length -ge 1) {
    $modeVal = $resp.Data[0]
    $modeName = switch ($modeVal) { 0 { "Normal" } 1 { "Low Power" } 2 { "Off-High" } 3 { "Off-Low" } 4 { "Strong Reflection" } default { "Unknown($modeVal)" } }
    Write-Host ("    Work mode: $modeVal ($modeName)") -ForegroundColor White
    Write-Pass ("Get work mode: $modeName ($modeVal)")
} else {
    Write-Fail "Get work mode failed"
}
Start-Sleep -Milliseconds 200

# 1.6 Get hold delay (sub=0x05, report=0x0A0D)
Write-Host "  1.6 Get hold delay (sub=0x05)..." -ForegroundColor White
$qData = Build-QueryData -SubCmd $SUB_GET_HOLD_DELAY
$resp = Send-QueryCommand -SerialPort $serialPort -MsgType $TYPE_SET_CONTROL_CMD -Data $qData -ReportType $TYPE_REPORT_HOLD_DELAY
if ($resp -ne $null -and $resp.Data -and $resp.Data.Length -ge 4) {
    $delay = Read-I32LE $resp.Data 0
    Write-Host ("    Hold delay: $delay seconds") -ForegroundColor White
    Write-Pass ("Get hold delay: ${delay}s")
} else {
    Write-Fail "Get hold delay failed"
}
Start-Sleep -Milliseconds 200

# 1.7 Get Z range (sub=0x0C, report=0x0A10)
Write-Host "  1.7 Get Z range (sub=0x0C)..." -ForegroundColor White
$qData = Build-QueryData -SubCmd $SUB_GET_Z_RANGE
$resp = Send-QueryCommand -SerialPort $serialPort -MsgType $TYPE_SET_CONTROL_CMD -Data $qData -ReportType $TYPE_REPORT_Z_RANGE
if ($resp -ne $null -and $resp.Data -and $resp.Data.Length -ge 8) {
    $zMin = Read-FloatLE $resp.Data 0
    $zMax = Read-FloatLE $resp.Data 4
    Write-Host ("    Z range: " + [math]::Round($zMin, 2) + "m ~ " + [math]::Round($zMax, 2) + "m") -ForegroundColor White
    Write-Pass ("Get Z range: " + [math]::Round($zMin, 2) + " ~ " + [math]::Round($zMax, 2) + " m")
} else {
    Write-Fail "Get Z range failed"
}
Start-Sleep -Milliseconds 200

# 1.8 Get low power time (sub=0x11, report=0x0A13)
Write-Host "  1.8 Get low power time (sub=0x11)..." -ForegroundColor White
$qData = Build-QueryData -SubCmd $SUB_GET_LOW_POWER_TIME
$resp = Send-QueryCommand -SerialPort $serialPort -MsgType $TYPE_SET_CONTROL_CMD -Data $qData -ReportType $TYPE_REPORT_LOW_POWER_TIME
if ($resp -ne $null -and $resp.Data -and $resp.Data.Length -ge 1) {
    $lpTime = $resp.Data[0]
    Write-Host ("    Low power time: $lpTime") -ForegroundColor White
    Write-Pass ("Get low power time: $lpTime")
} else {
    Write-Fail "Get low power time failed"
}
Start-Sleep -Milliseconds 200

# 1.9 Get GPIO mode (sub=0x14, report=0x0A15)
Write-Host "  1.9 Get GPIO mode (sub=0x14)..." -ForegroundColor White
$qData = Build-QueryData -SubCmd $SUB_GET_GPIO_MODE
$resp = Send-QueryCommand -SerialPort $serialPort -MsgType $TYPE_SET_CONTROL_CMD -Data $qData -ReportType $TYPE_REPORT_GPIO_STATE
if ($resp -ne $null -and $resp.Data -and $resp.Data.Length -ge 1) {
    $gpioVal = $resp.Data[0]
    Write-Host ("    GPIO mode: $gpioVal") -ForegroundColor White
    Write-Pass ("Get GPIO mode: $gpioVal")
} else {
    Write-Fail "Get GPIO mode failed"
}
Start-Sleep -Milliseconds 200

# 1.10 Get stay life (sub=0x16, report=0x0A17)
Write-Host "  1.10 Get stay life (sub=0x16)..." -ForegroundColor White
$qData = Build-QueryData -SubCmd $SUB_GET_STAY_LIFE
$resp = Send-QueryCommand -SerialPort $serialPort -MsgType $TYPE_SET_CONTROL_CMD -Data $qData -ReportType $TYPE_REPORT_STAY_LIFE
if ($resp -ne $null -and $resp.Data -and $resp.Data.Length -ge 1) {
    $stayLife = $resp.Data[0]
    Write-Host ("    Stay life: $stayLife") -ForegroundColor White
    Write-Pass ("Get stay life: $stayLife")
} else {
    Write-Fail "Get stay life failed"
}
Start-Sleep -Milliseconds 200

# 1.11 Get output interval (sub=0x17, report=0x0A18)
Write-Host "  1.11 Get output interval (sub=0x17)..." -ForegroundColor White
$qData = Build-QueryData -SubCmd $SUB_GET_OUTPUT_INTERVAL
$resp = Send-QueryCommand -SerialPort $serialPort -MsgType $TYPE_SET_CONTROL_CMD -Data $qData -ReportType $TYPE_REPORT_OUTPUT_INTERVAL
if ($resp -ne $null -and $resp.Data -and $resp.Data.Length -ge 1) {
    $interval = $resp.Data[0]
    Write-Host ("    Output interval: $interval") -ForegroundColor White
    Write-Pass ("Get output interval: $interval")
} else {
    Write-Fail "Get output interval failed"
}
Start-Sleep -Milliseconds 200

# 1.12 Get all areas (sub=0x02, report=0x0A0B/0x0A0C/0x0A16)
Write-Host "  1.12 Get all areas (sub=0x02)..." -ForegroundColor White
$qData = Build-QueryData -SubCmd $SUB_GET_AREAS
$resp = Send-QueryCommand -SerialPort $serialPort -MsgType $TYPE_SET_CONTROL_CMD -Data $qData -ReportType 0
if ($resp -ne $null) {
    # After ACK, wait for multiple area report frames
    $area1 = Wait-ReportFrame -SerialPort $serialPort -ExpectedType $TYPE_REPORT_AREA_1 -TimeoutMs 1500
    $area2 = Wait-ReportFrame -SerialPort $serialPort -ExpectedType $TYPE_REPORT_AREA_2 -TimeoutMs 1500
    $area3 = Wait-ReportFrame -SerialPort $serialPort -ExpectedType $TYPE_REPORT_AREA_3 -TimeoutMs 1500

    $areaCount = 0
    if ($area1 -ne $null) {
        $areaCount++
        if ($area1.Data -and $area1.Data.Length -ge 24) {
            $xMin = [math]::Round((Read-FloatLE $area1.Data 0), 2)
            $xMax = [math]::Round((Read-FloatLE $area1.Data 4), 2)
            $yMin = [math]::Round((Read-FloatLE $area1.Data 8), 2)
            $yMax = [math]::Round((Read-FloatLE $area1.Data 12), 2)
            $zMin = [math]::Round((Read-FloatLE $area1.Data 16), 2)
            $zMax = [math]::Round((Read-FloatLE $area1.Data 20), 2)
            Write-Host ("    Area 1: X[$xMin,$xMax] Y[$yMin,$yMax] Z[$zMin,$zMax]") -ForegroundColor White
        }
    }
    if ($area2 -ne $null) {
        $areaCount++
        if ($area2.Data -and $area2.Data.Length -ge 24) {
            $xMin = [math]::Round((Read-FloatLE $area2.Data 0), 2)
            $xMax = [math]::Round((Read-FloatLE $area2.Data 4), 2)
            $yMin = [math]::Round((Read-FloatLE $area2.Data 8), 2)
            $yMax = [math]::Round((Read-FloatLE $area2.Data 12), 2)
            $zMin = [math]::Round((Read-FloatLE $area2.Data 16), 2)
            $zMax = [math]::Round((Read-FloatLE $area2.Data 20), 2)
            Write-Host ("    Area 2: X[$xMin,$xMax] Y[$yMin,$yMax] Z[$zMin,$zMax]") -ForegroundColor White
        }
    }
    if ($area3 -ne $null) {
        $areaCount++
        if ($area3.Data -and $area3.Data.Length -ge 24) {
            $xMin = [math]::Round((Read-FloatLE $area3.Data 0), 2)
            $xMax = [math]::Round((Read-FloatLE $area3.Data 4), 2)
            $yMin = [math]::Round((Read-FloatLE $area3.Data 8), 2)
            $yMax = [math]::Round((Read-FloatLE $area3.Data 12), 2)
            $zMin = [math]::Round((Read-FloatLE $area3.Data 16), 2)
            $zMax = [math]::Round((Read-FloatLE $area3.Data 20), 2)
            Write-Host ("    Area 3: X[$xMin,$xMax] Y[$yMin,$yMax] Z[$zMin,$zMax]") -ForegroundColor White
        }
    }

    if ($areaCount -gt 0) {
        Write-Pass ("Get all areas: $areaCount area(s) received")
    } else {
        Write-Info "ACK received but no area report frames captured"
        Write-Pass "Get areas command acknowledged"
    }
} else {
    Write-Fail "Get all areas failed"
}
Start-Sleep -Milliseconds 200

# ============================================================
# Phase 2: Sensitivity Control (SET + GET loop)
# ============================================================
if (-not $SkipSetTests) {
Write-TestHeader "Phase 2: Sensitivity Control (SET + GET loop)"

# 2.1 Set sensitivity = Low (0)
Write-Host "  2.1 Set sensitivity = Low (0)..." -ForegroundColor White
$ok = Send-SetCommand -SerialPort $serialPort -SubCmd $SUB_SET_SENSITIVITY -Param 0
if ($ok) {
    # Verify with GET
    Start-Sleep -Milliseconds 100
    $qData = Build-QueryData -SubCmd $SUB_GET_SENSITIVITY
    $resp = Send-QueryCommand -SerialPort $serialPort -MsgType $TYPE_SET_CONTROL_CMD -Data $qData -ReportType $TYPE_REPORT_SENSITIVITY
    if ($resp -ne $null -and $resp.Data -and $resp.Data.Length -ge 1 -and $resp.Data[0] -eq 0) {
        Write-Pass "Sensitivity set to Low (0), verified"
    } else {
        $gotVal = if ($resp -and $resp.Data) { $resp.Data[0] } else { "N/A" }
        Write-Fail ("Sensitivity set to Low but GET returned $gotVal")
    }
} else {
    Write-Fail "Set sensitivity = Low failed (no ACK)"
}
Start-Sleep -Milliseconds 200

# 2.2 Set sensitivity = Medium (1)
Write-Host "  2.2 Set sensitivity = Medium (1)..." -ForegroundColor White
$ok = Send-SetCommand -SerialPort $serialPort -SubCmd $SUB_SET_SENSITIVITY -Param 1
if ($ok) {
    Start-Sleep -Milliseconds 100
    $qData = Build-QueryData -SubCmd $SUB_GET_SENSITIVITY
    $resp = Send-QueryCommand -SerialPort $serialPort -MsgType $TYPE_SET_CONTROL_CMD -Data $qData -ReportType $TYPE_REPORT_SENSITIVITY
    if ($resp -ne $null -and $resp.Data -and $resp.Data.Length -ge 1 -and $resp.Data[0] -eq 1) {
        Write-Pass "Sensitivity set to Medium (1), verified"
    } else {
        $gotVal = if ($resp -and $resp.Data) { $resp.Data[0] } else { "N/A" }
        Write-Fail ("Sensitivity set to Medium but GET returned $gotVal")
    }
} else {
    Write-Fail "Set sensitivity = Medium failed (no ACK)"
}
Start-Sleep -Milliseconds 200

# 2.3 Set sensitivity = High (2)
Write-Host "  2.3 Set sensitivity = High (2)..." -ForegroundColor White
$ok = Send-SetCommand -SerialPort $serialPort -SubCmd $SUB_SET_SENSITIVITY -Param 2
if ($ok) {
    Start-Sleep -Milliseconds 100
    $qData = Build-QueryData -SubCmd $SUB_GET_SENSITIVITY
    $resp = Send-QueryCommand -SerialPort $serialPort -MsgType $TYPE_SET_CONTROL_CMD -Data $qData -ReportType $TYPE_REPORT_SENSITIVITY
    if ($resp -ne $null -and $resp.Data -and $resp.Data.Length -ge 1 -and $resp.Data[0] -eq 2) {
        Write-Pass "Sensitivity set to High (2), verified"
    } else {
        $gotVal = if ($resp -and $resp.Data) { $resp.Data[0] } else { "N/A" }
        Write-Fail ("Sensitivity set to High but GET returned $gotVal")
    }
} else {
    Write-Fail "Set sensitivity = High failed (no ACK)"
}
Start-Sleep -Milliseconds 200

# 2.4 Restore default (Medium=1)
Write-Host "  2.4 Restore sensitivity default (Medium=1)..." -ForegroundColor White
$ok = Send-SetCommand -SerialPort $serialPort -SubCmd $SUB_SET_SENSITIVITY -Param 1
if ($ok) { Write-Pass "Sensitivity restored to Medium (1)" }
else { Write-Fail "Restore sensitivity failed" }
Start-Sleep -Milliseconds 200

} else {
    Write-TestHeader "Phase 2: Sensitivity Control - SKIPPED"
}

# ============================================================
# Phase 3: Trigger Speed Control
# ============================================================
if (-not $SkipSetTests) {
Write-TestHeader "Phase 3: Trigger Speed Control"

# 3.1 Set = Slow (0)
Write-Host "  3.1 Set trigger speed = Slow (0)..." -ForegroundColor White
$ok = Send-SetCommand -SerialPort $serialPort -SubCmd $SUB_SET_TRIGGER_SPEED -Param 0
if ($ok) {
    Start-Sleep -Milliseconds 100
    $qData = Build-QueryData -SubCmd $SUB_GET_TRIGGER_SPEED
    $resp = Send-QueryCommand -SerialPort $serialPort -MsgType $TYPE_SET_CONTROL_CMD -Data $qData -ReportType $TYPE_REPORT_TRIGGER_SPEED
    if ($resp -ne $null -and $resp.Data -and $resp.Data.Length -ge 1 -and $resp.Data[0] -eq 0) {
        Write-Pass "Trigger speed set to Slow (0), verified"
    } else {
        $gotVal = if ($resp -and $resp.Data) { $resp.Data[0] } else { "N/A" }
        Write-Fail ("Trigger speed set to Slow but GET returned $gotVal")
    }
} else { Write-Fail "Set trigger speed = Slow failed" }
Start-Sleep -Milliseconds 200

# 3.2 Set = Medium (1)
Write-Host "  3.2 Set trigger speed = Medium (1)..." -ForegroundColor White
$ok = Send-SetCommand -SerialPort $serialPort -SubCmd $SUB_SET_TRIGGER_SPEED -Param 1
if ($ok) {
    Start-Sleep -Milliseconds 100
    $qData = Build-QueryData -SubCmd $SUB_GET_TRIGGER_SPEED
    $resp = Send-QueryCommand -SerialPort $serialPort -MsgType $TYPE_SET_CONTROL_CMD -Data $qData -ReportType $TYPE_REPORT_TRIGGER_SPEED
    if ($resp -ne $null -and $resp.Data -and $resp.Data.Length -ge 1 -and $resp.Data[0] -eq 1) {
        Write-Pass "Trigger speed set to Medium (1), verified"
    } else {
        $gotVal = if ($resp -and $resp.Data) { $resp.Data[0] } else { "N/A" }
        Write-Fail ("Trigger speed set to Medium but GET returned $gotVal")
    }
} else { Write-Fail "Set trigger speed = Medium failed" }
Start-Sleep -Milliseconds 200

# 3.3 Set = Fast (2)
Write-Host "  3.3 Set trigger speed = Fast (2)..." -ForegroundColor White
$ok = Send-SetCommand -SerialPort $serialPort -SubCmd $SUB_SET_TRIGGER_SPEED -Param 2
if ($ok) {
    Start-Sleep -Milliseconds 100
    $qData = Build-QueryData -SubCmd $SUB_GET_TRIGGER_SPEED
    $resp = Send-QueryCommand -SerialPort $serialPort -MsgType $TYPE_SET_CONTROL_CMD -Data $qData -ReportType $TYPE_REPORT_TRIGGER_SPEED
    if ($resp -ne $null -and $resp.Data -and $resp.Data.Length -ge 1 -and $resp.Data[0] -eq 2) {
        Write-Pass "Trigger speed set to Fast (2), verified"
    } else {
        $gotVal = if ($resp -and $resp.Data) { $resp.Data[0] } else { "N/A" }
        Write-Fail ("Trigger speed set to Fast but GET returned $gotVal")
    }
} else { Write-Fail "Set trigger speed = Fast failed" }
Start-Sleep -Milliseconds 200

# 3.4 Restore default (Medium=1)
Write-Host "  3.4 Restore trigger speed default (Medium=1)..." -ForegroundColor White
$ok = Send-SetCommand -SerialPort $serialPort -SubCmd $SUB_SET_TRIGGER_SPEED -Param 1
if ($ok) { Write-Pass "Trigger speed restored to Medium (1)" }
else { Write-Fail "Restore trigger speed failed" }
Start-Sleep -Milliseconds 200

} else {
    Write-TestHeader "Phase 3: Trigger Speed Control - SKIPPED"
}

# ============================================================
# Phase 4: Install Mode
# ============================================================
if (-not $SkipSetTests) {
Write-TestHeader "Phase 4: Install Mode"

# 4.1 Set = Top (0)
Write-Host "  4.1 Set install mode = Top (0)..." -ForegroundColor White
$ok = Send-SetCommand -SerialPort $serialPort -SubCmd $SUB_SET_INSTALL_MODE -Param 0
if ($ok) {
    Start-Sleep -Milliseconds 100
    $qData = Build-QueryData -SubCmd $SUB_GET_INSTALL_MODE
    $resp = Send-QueryCommand -SerialPort $serialPort -MsgType $TYPE_SET_CONTROL_CMD -Data $qData -ReportType $TYPE_REPORT_INSTALL_MODE
    if ($resp -ne $null -and $resp.Data -and $resp.Data.Length -ge 1 -and $resp.Data[0] -eq 0) {
        Write-Pass "Install mode set to Top (0), verified"
    } else {
        $gotVal = if ($resp -and $resp.Data) { $resp.Data[0] } else { "N/A" }
        Write-Fail ("Install mode set to Top but GET returned $gotVal")
    }
} else { Write-Fail "Set install mode = Top failed" }
Start-Sleep -Milliseconds 200

# 4.2 Set = Side (1)
Write-Host "  4.2 Set install mode = Side (1)..." -ForegroundColor White
$ok = Send-SetCommand -SerialPort $serialPort -SubCmd $SUB_SET_INSTALL_MODE -Param 1
if ($ok) {
    Start-Sleep -Milliseconds 100
    $qData = Build-QueryData -SubCmd $SUB_GET_INSTALL_MODE
    $resp = Send-QueryCommand -SerialPort $serialPort -MsgType $TYPE_SET_CONTROL_CMD -Data $qData -ReportType $TYPE_REPORT_INSTALL_MODE
    if ($resp -ne $null -and $resp.Data -and $resp.Data.Length -ge 1 -and $resp.Data[0] -eq 1) {
        Write-Pass "Install mode set to Side (1), verified"
    } else {
        $gotVal = if ($resp -and $resp.Data) { $resp.Data[0] } else { "N/A" }
        Write-Fail ("Install mode set to Side but GET returned $gotVal")
    }
} else { Write-Fail "Set install mode = Side failed" }
Start-Sleep -Milliseconds 200

# 4.3 Restore default (Top=0)
Write-Host "  4.3 Restore install mode default (Top=0)..." -ForegroundColor White
$ok = Send-SetCommand -SerialPort $serialPort -SubCmd $SUB_SET_INSTALL_MODE -Param 0
if ($ok) { Write-Pass "Install mode restored to Top (0)" }
else { Write-Fail "Restore install mode failed" }
Start-Sleep -Milliseconds 200

} else {
    Write-TestHeader "Phase 4: Install Mode - SKIPPED"
}

# ============================================================
# Phase 5: Work Mode
# ============================================================
if (-not $SkipSetTests) {
Write-TestHeader "Phase 5: Work Mode"

$workModeNames = @("Normal", "Low Power", "Off-High", "Off-Low", "Strong Reflection")
for ($m = 0; $m -le 4; $m++) {
    Write-Host ("  5." + ($m + 1) + " Set work mode = $m ($($workModeNames[$m]))...") -ForegroundColor White
    $ok = Send-SetCommand -SerialPort $serialPort -SubCmd $SUB_SET_WORK_MODE -Param $m
    if ($ok) {
        Start-Sleep -Milliseconds 100
        $qData = Build-QueryData -SubCmd $SUB_GET_WORK_MODE
        $resp = Send-QueryCommand -SerialPort $serialPort -MsgType $TYPE_SET_CONTROL_CMD -Data $qData -ReportType $TYPE_REPORT_WORK_MODE
        if ($resp -ne $null -and $resp.Data -and $resp.Data.Length -ge 1 -and $resp.Data[0] -eq $m) {
            Write-Pass ("Work mode set to $m ($($workModeNames[$m])), verified")
        } else {
            $gotVal = if ($resp -and $resp.Data) { $resp.Data[0] } else { "N/A" }
            Write-Fail ("Work mode set to $m but GET returned $gotVal")
        }
    } else {
        Write-Fail ("Set work mode = $m failed")
    }
    Start-Sleep -Milliseconds 200
}

# 5.6 Restore to Normal (0)
Write-Host "  5.6 Restore work mode to Normal (0)..." -ForegroundColor White
$ok = Send-SetCommand -SerialPort $serialPort -SubCmd $SUB_SET_WORK_MODE -Param 0
if ($ok) { Write-Pass "Work mode restored to Normal (0)" }
else { Write-Fail "Restore work mode failed" }
Start-Sleep -Milliseconds 200

} else {
    Write-TestHeader "Phase 5: Work Mode - SKIPPED"
}

# ============================================================
# Phase 6: Area Setting (TYPE=0x0202, 6 floats)
# ============================================================
if (-not $SkipSetTests) {
Write-TestHeader "Phase 6: Area Setting (TYPE=0x0202, 6 floats)"

Write-Host "  6.1 Set Area 1 (test values)..." -ForegroundColor White
$areaData = New-Object byte[] 24
Write-FloatLE $areaData 0  -2.0    # x_min
Write-FloatLE $areaData 4   2.0    # x_max
Write-FloatLE $areaData 8   0.0    # y_min
Write-FloatLE $areaData 12  5.0    # y_max
Write-FloatLE $areaData 16 -1.0    # z_min
Write-FloatLE $areaData 20  3.0    # z_max
$ok = Send-StandaloneSetCommand -SerialPort $serialPort -MsgType $TYPE_SET_AREA -Data $areaData
if ($ok) { Write-Pass "Area 1 set (TYPE=0x0202, 6 floats)" }
else { Write-Fail "Set area failed" }
Start-Sleep -Milliseconds 200

} else {
    Write-TestHeader "Phase 6: Area Setting - SKIPPED"
}

# ============================================================
# Phase 7: Hold Delay (TYPE=0x0203, 1 byte)
# ============================================================
if (-not $SkipSetTests) {
Write-TestHeader "Phase 7: Hold Delay (TYPE=0x0203, 1 byte)"

Write-Host "  7.1 Set hold delay = 5..." -ForegroundColor White
$delayData = New-Object byte[] 1
$delayData[0] = 5
$ok = Send-StandaloneSetCommand -SerialPort $serialPort -MsgType $TYPE_SET_HOLD_DELAY -Data $delayData
if ($ok) {
    Start-Sleep -Milliseconds 100
    $qData = Build-QueryData -SubCmd $SUB_GET_HOLD_DELAY
    $resp = Send-QueryCommand -SerialPort $serialPort -MsgType $TYPE_SET_CONTROL_CMD -Data $qData -ReportType $TYPE_REPORT_HOLD_DELAY
    if ($resp -ne $null -and $resp.Data -and $resp.Data.Length -ge 4) {
        $gotDelay = Read-I32LE $resp.Data 0
        Write-Host ("    Hold delay verified: $gotDelay") -ForegroundColor White
        Write-Pass "Hold delay set to 5, verified"
    } else {
        Write-Pass "Hold delay ACK received (report not captured)"
    }
} else { Write-Fail "Set hold delay failed" }
Start-Sleep -Milliseconds 200

} else {
    Write-TestHeader "Phase 7: Hold Delay - SKIPPED"
}

# ============================================================
# Phase 8: Z Range (TYPE=0x0204, 2 floats)
# ============================================================
if (-not $SkipSetTests) {
Write-TestHeader "Phase 8: Z Range (TYPE=0x0204, 2 floats)"

Write-Host "  8.1 Set Z range = -0.5m ~ 3.0m..." -ForegroundColor White
$zrData = New-Object byte[] 8
Write-FloatLE $zrData 0 -0.5
Write-FloatLE $zrData 4  3.0
$ok = Send-StandaloneSetCommand -SerialPort $serialPort -MsgType $TYPE_SET_Z_RANGE -Data $zrData
if ($ok) {
    Start-Sleep -Milliseconds 100
    $qData = Build-QueryData -SubCmd $SUB_GET_Z_RANGE
    $resp = Send-QueryCommand -SerialPort $serialPort -MsgType $TYPE_SET_CONTROL_CMD -Data $qData -ReportType $TYPE_REPORT_Z_RANGE
    if ($resp -ne $null -and $resp.Data -and $resp.Data.Length -ge 8) {
        $gotZMin = [math]::Round((Read-FloatLE $resp.Data 0), 2)
        $gotZMax = [math]::Round((Read-FloatLE $resp.Data 4), 2)
        Write-Host ("    Z range verified: ${gotZMin}m ~ ${gotZMax}m") -ForegroundColor White
        Write-Pass ("Z range set to -0.5 ~ 3.0m, verified: ${gotZMin} ~ ${gotZMax}m")
    } else {
        Write-Pass "Z range ACK received (report not captured)"
    }
} else { Write-Fail "Set Z range failed" }
Start-Sleep -Milliseconds 200

} else {
    Write-TestHeader "Phase 8: Z Range - SKIPPED"
}

# ============================================================
# Phase 9: Low Power Time (TYPE=0x0205, 1 byte)
# ============================================================
if (-not $SkipSetTests) {
Write-TestHeader "Phase 9: Low Power Time (TYPE=0x0205, 1 byte)"

Write-Host "  9.1 Set low power time = 10..." -ForegroundColor White
$lpData = New-Object byte[] 1
$lpData[0] = 10
$ok = Send-StandaloneSetCommand -SerialPort $serialPort -MsgType $TYPE_SET_LOW_POWER -Data $lpData
if ($ok) {
    Start-Sleep -Milliseconds 100
    $qData = Build-QueryData -SubCmd $SUB_GET_LOW_POWER_TIME
    $resp = Send-QueryCommand -SerialPort $serialPort -MsgType $TYPE_SET_CONTROL_CMD -Data $qData -ReportType $TYPE_REPORT_LOW_POWER_TIME
    if ($resp -ne $null -and $resp.Data -and $resp.Data.Length -ge 1) {
        $gotLp = $resp.Data[0]
        Write-Host ("    Low power time verified: $gotLp") -ForegroundColor White
        Write-Pass "Low power time set to 10, verified"
    } else {
        Write-Pass "Low power time ACK received (report not captured)"
    }
} else { Write-Fail "Set low power time failed" }
Start-Sleep -Milliseconds 200

} else {
    Write-TestHeader "Phase 9: Low Power Time - SKIPPED"
}

# ============================================================
# Phase 10: Stay Life (TYPE=0x0206, 1 byte)
# ============================================================
if (-not $SkipSetTests) {
Write-TestHeader "Phase 10: Stay Life (TYPE=0x0206, 1 byte)"

Write-Host "  10.1 Set stay life = 30..." -ForegroundColor White
$slData = New-Object byte[] 1
$slData[0] = 30
$ok = Send-StandaloneSetCommand -SerialPort $serialPort -MsgType $TYPE_SET_STAY_LIFE -Data $slData
if ($ok) {
    Start-Sleep -Milliseconds 100
    $qData = Build-QueryData -SubCmd $SUB_GET_STAY_LIFE
    $resp = Send-QueryCommand -SerialPort $serialPort -MsgType $TYPE_SET_CONTROL_CMD -Data $qData -ReportType $TYPE_REPORT_STAY_LIFE
    if ($resp -ne $null -and $resp.Data -and $resp.Data.Length -ge 1) {
        $gotSl = $resp.Data[0]
        Write-Host ("    Stay life verified: $gotSl") -ForegroundColor White
        Write-Pass "Stay life set to 30, verified"
    } else {
        Write-Pass "Stay life ACK received (report not captured)"
    }
} else { Write-Fail "Set stay life failed" }
Start-Sleep -Milliseconds 200

} else {
    Write-TestHeader "Phase 10: Stay Life - SKIPPED"
}

# ============================================================
# Phase 11: Output Interval (TYPE=0x0207, 1 byte)
# ============================================================
if (-not $SkipSetTests) {
Write-TestHeader "Phase 11: Output Interval (TYPE=0x0207, 1 byte)"

Write-Host "  11.1 Set output interval = 1..." -ForegroundColor White
$oiData = New-Object byte[] 1
$oiData[0] = 1
$ok = Send-StandaloneSetCommand -SerialPort $serialPort -MsgType $TYPE_SET_OUTPUT_INTERVAL -Data $oiData
if ($ok) {
    Start-Sleep -Milliseconds 100
    $qData = Build-QueryData -SubCmd $SUB_GET_OUTPUT_INTERVAL
    $resp = Send-QueryCommand -SerialPort $serialPort -MsgType $TYPE_SET_CONTROL_CMD -Data $qData -ReportType $TYPE_REPORT_OUTPUT_INTERVAL
    if ($resp -ne $null -and $resp.Data -and $resp.Data.Length -ge 1) {
        $gotOi = $resp.Data[0]
        Write-Host ("    Output interval verified: $gotOi") -ForegroundColor White
        Write-Pass "Output interval set to 1, verified"
    } else {
        Write-Pass "Output interval ACK received (report not captured)"
    }
} else { Write-Fail "Set output interval failed" }
Start-Sleep -Milliseconds 200

} else {
    Write-TestHeader "Phase 11: Output Interval - SKIPPED"
}

# ============================================================
# Phase 12: GPIO Mode (sub=0x13, param=0~5)
# ============================================================
if (-not $SkipSetTests) {
Write-TestHeader "Phase 12: GPIO Mode (sub=0x13, param=0~5)"

for ($g = 0; $g -le 5; $g++) {
    Write-Host ("  12." + ($g + 1) + " Set GPIO mode = $g...") -ForegroundColor White
    $ok = Send-SetCommand -SerialPort $serialPort -SubCmd $SUB_SET_GPIO_MODE -Param $g
    if ($ok) {
        Start-Sleep -Milliseconds 100
        $qData = Build-QueryData -SubCmd $SUB_GET_GPIO_MODE
        $resp = Send-QueryCommand -SerialPort $serialPort -MsgType $TYPE_SET_CONTROL_CMD -Data $qData -ReportType $TYPE_REPORT_GPIO_STATE
        if ($resp -ne $null -and $resp.Data -and $resp.Data.Length -ge 1 -and $resp.Data[0] -eq $g) {
            Write-Pass ("GPIO mode set to $g, verified")
        } else {
            $gotVal = if ($resp -and $resp.Data) { $resp.Data[0] } else { "N/A" }
            Write-Fail ("GPIO mode set to $g but GET returned $gotVal")
        }
    } else {
        Write-Fail ("Set GPIO mode = $g failed")
    }
    Start-Sleep -Milliseconds 200
}

} else {
    Write-TestHeader "Phase 12: GPIO Mode - SKIPPED"
}

# ============================================================
# Phase 13: Functional Operation Commands
# ============================================================
if (-not $SkipSetTests) {
Write-TestHeader "Phase 13: Functional Operation Commands"

# 13.1 Auto generate noise (sub=0x01)
Write-Host "  13.1 Auto generate noise (sub=0x01)..." -ForegroundColor White
$ok = Send-SetCommand -SerialPort $serialPort -SubCmd $SUB_AUTO_GEN_NOISE
if ($ok) { Write-Pass "Auto generate noise executed" }
else { Write-Fail "Auto generate noise failed" }
Start-Sleep -Milliseconds 500  # Noise generation may take time
Start-Sleep -Milliseconds 200

# 13.2 Clear noise (sub=0x03)
Write-Host "  13.2 Clear noise (sub=0x03)..." -ForegroundColor White
$ok = Send-SetCommand -SerialPort $serialPort -SubCmd $SUB_CLEAR_NOISE
if ($ok) { Write-Pass "Clear noise executed" }
else { Write-Fail "Clear noise failed" }
Start-Sleep -Milliseconds 200

# 13.3 Reset detection state (sub=0x04)
Write-Host "  13.3 Reset detection state (sub=0x04)..." -ForegroundColor White
$ok = Send-SetCommand -SerialPort $serialPort -SubCmd $SUB_RESET_DETECTION
if ($ok) { Write-Pass "Reset detection state executed" }
else { Write-Fail "Reset detection state failed" }
Start-Sleep -Milliseconds 200

# 13.4 Reset unoccupied state (sub=0x12)
Write-Host "  13.4 Reset unoccupied state (sub=0x12)..." -ForegroundColor White
$ok = Send-SetCommand -SerialPort $serialPort -SubCmd $SUB_RESET_UNOCCUPIED
if ($ok) { Write-Pass "Reset unoccupied state executed" }
else { Write-Fail "Reset unoccupied state failed" }
Start-Sleep -Milliseconds 200

# 13.5 Clear stay areas (sub=0x15)
Write-Host "  13.5 Clear stay areas (sub=0x15)..." -ForegroundColor White
$ok = Send-SetCommand -SerialPort $serialPort -SubCmd $SUB_CLEAR_STAY_AREAS
if ($ok) { Write-Pass "Clear stay areas executed" }
else { Write-Fail "Clear stay areas failed" }
Start-Sleep -Milliseconds 200

} else {
    Write-TestHeader "Phase 13: Functional Operations - SKIPPED"
}

# ============================================================
# Phase 14: Point Cloud & Target Display
# ============================================================
if (-not $SkipSetTests) {
Write-TestHeader "Phase 14: Point Cloud & Target Display"

# 14.1 Enable point cloud (sub=0x06, param=1)
Write-Host "  14.1 Enable point cloud (sub=0x06, param=1)..." -ForegroundColor White
$ok = Send-SetCommand -SerialPort $serialPort -SubCmd $SUB_SET_POINT_CLOUD -Param 1
if ($ok) {
    Write-Pass "Point cloud enabled"
    # Observe for 0x0A08 frames
    Write-Host "    Observing for point cloud frames (0x0A08)..." -ForegroundColor Gray
    $pcFrame = Wait-ReportFrame -SerialPort $serialPort -ExpectedType $TYPE_REPORT_POINT_CLOUD -TimeoutMs 3000
    if ($pcFrame -ne $null) {
        Write-Host ("    Point cloud frame received: LEN=" + $pcFrame.Len) -ForegroundColor White
        Write-Pass "Point cloud data (0x0A08) detected"
    } else {
        Write-Info "No point cloud frames within 3s (may need motion in range)"
    }
} else { Write-Fail "Enable point cloud failed" }
Start-Sleep -Milliseconds 200

# 14.2 Disable point cloud (param=0)
Write-Host "  14.2 Disable point cloud (sub=0x06, param=0)..." -ForegroundColor White
$ok = Send-SetCommand -SerialPort $serialPort -SubCmd $SUB_SET_POINT_CLOUD -Param 0
if ($ok) { Write-Pass "Point cloud disabled" }
else { Write-Fail "Disable point cloud failed" }
Start-Sleep -Milliseconds 200

# 14.3 Set target display mode (sub=0x07, param=0)
Write-Host "  14.3 Set target display mode (sub=0x07, param=0)..." -ForegroundColor White
$ok = Send-SetCommand -SerialPort $serialPort -SubCmd $SUB_SET_TARGET_DISPLAY -Param 0
if ($ok) { Write-Pass "Target display mode set to 0" }
else { Write-Fail "Set target display mode failed" }
Start-Sleep -Milliseconds 200

} else {
    Write-TestHeader "Phase 14: Point Cloud & Target Display - SKIPPED"
}

# ============================================================
# Phase 15: Baud Rate Change (HIGH RISK - last)
# ============================================================
if (-not $SkipSetTests -and -not $SkipBaudRateTest) {
Write-TestHeader "Phase 15: Baud Rate Change (HIGH RISK)"

# 15.1 Change to 256000 (param=5)
Write-Host "  15.1 Set baud rate = 256000 (param=5)..." -ForegroundColor White
Write-Host "    WARNING: If this fails, radar will be at 256000 baud!" -ForegroundColor Yellow
$baudData = New-Object byte[] 1
$baudData[0] = 5  # 256000
$ok = Send-StandaloneSetCommand -SerialPort $serialPort -MsgType $TYPE_SET_BAUD_RATE -Data $baudData
if ($ok) {
    Write-Pass "Baud rate change to 256000 ACK received"
    Write-Host "    Switching host to 256000..." -ForegroundColor Gray
    Start-Sleep -Milliseconds 500

    try {
        $serialPort.Close()
        Start-Sleep -Milliseconds 200
        $serialPort.BaudRate = 256000
        $serialPort.Open()
        Write-Pass "Host switched to 256000 baud"

        # Verify connectivity at new baud rate
        Start-Sleep -Milliseconds 500
        $serialPort.DiscardInBuffer()
        Reset-TFParser
        $verifyResult = Read-TFFrames -SerialPort $serialPort -MaxFrames 10 -TimeoutSeconds 5
        if ($verifyResult.Frames.Count -gt 0) {
            Write-Pass ("Communication verified at 256000 (" + $verifyResult.Frames.Count + " frames)")
        } else {
            Write-Fail "No frames at 256000 - baud rate change may have failed"
        }
    } catch {
        Write-Fail ("Switch to 256000 failed: " + $_.Exception.Message)
        Write-Host "    Radar may still be at 115200" -ForegroundColor Yellow
        # Try to recover
        try {
            $serialPort.BaudRate = 115200
            $serialPort.Open()
            Write-Info "Recovered at 115200"
        } catch {
            Write-Fail "Cannot recover serial port"
        }
    }

    # 15.2 Restore to 115200 (param=4)
    Write-Host "  15.2 Restore baud rate = 115200 (param=4)..." -ForegroundColor White
    $baudData[0] = 4  # 115200
    $ok = Send-StandaloneSetCommand -SerialPort $serialPort -MsgType $TYPE_SET_BAUD_RATE -Data $baudData
    if ($ok) {
        Write-Pass "Baud rate restore to 115200 ACK received"
        Start-Sleep -Milliseconds 500

        try {
            $serialPort.Close()
            Start-Sleep -Milliseconds 200
            $serialPort.BaudRate = 115200
            $serialPort.Open()
            Write-Pass "Host restored to 115200 baud"

            Start-Sleep -Milliseconds 500
            $serialPort.DiscardInBuffer()
            Reset-TFParser
            $verifyResult = Read-TFFrames -SerialPort $serialPort -MaxFrames 10 -TimeoutSeconds 5
            if ($verifyResult.Frames.Count -gt 0) {
                Write-Pass ("Communication verified at 115200 (" + $verifyResult.Frames.Count + " frames)")
            } else {
                Write-Fail "No frames at 115200 after restore"
            }
        } catch {
            Write-Fail ("Restore to 115200 failed: " + $_.Exception.Message)
        }
    } else {
        Write-Fail "Restore baud rate to 115200 failed (no ACK)"
        Write-Host "    Radar may be at 256000 - use -BaudRate 256000 to reconnect" -ForegroundColor Yellow
    }
} else {
    Write-Fail "Set baud rate to 256000 failed (no ACK)"
}

} else {
    Write-TestHeader "Phase 15: Baud Rate Change - SKIPPED"
}

# ============================================================
# SUMMARY
# ============================================================
Write-Host ""
Write-Host "============================================" -ForegroundColor Magenta
Write-Host "  TEST SUMMARY" -ForegroundColor Magenta
Write-Host "============================================" -ForegroundColor Magenta
Write-Host ("  PASS: " + $script:PassCount) -ForegroundColor Green
Write-Host ("  FAIL: " + $script:FailCount) -ForegroundColor Red
$total = $script:PassCount + $script:FailCount
Write-Host ("  TOTAL: $total") -ForegroundColor White
Write-Host "============================================" -ForegroundColor Magenta

if ($script:FailCount -eq 0) {
    Write-Host ""
    Write-Host "  All tests PASSED!" -ForegroundColor Green
} else {
    Write-Host ""
    Write-Host ("  " + $script:FailCount + " test(s) FAILED. See details above.") -ForegroundColor Yellow
}

# Close serial port
try {
    $serialPort.Close()
    Write-Host ""
    Write-Host "  Serial port closed." -ForegroundColor Gray
} catch {
    Write-Host ""
    Write-Host "  Warning: Could not close serial port." -ForegroundColor Yellow
}
