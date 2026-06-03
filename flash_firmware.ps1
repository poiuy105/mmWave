#requires -Version 5.1
<#
.SYNOPSIS
    一键编译烧录 ESP32 固件脚本
.DESCRIPTION
    1. Push 代码到 GitHub
    2. 等待 GitHub Actions 编译完成
    3. 下载 merged.bin
    4. 烧录到 ESP32
.NOTES
    需要提前安装: git, gh CLI, esptool
    需要 gh 已登录: gh auth login
#>

param(
    [string]$Repo = "poiuy105/mmWave",
    [string]$Port = "COM33",
    [int]$Baud = 921600,
    [string]$ArtifactName = "ld_radar_monitor_merged",
    [string]$DownloadDir = "$env:TEMP\mmwave_firmware",
    [int]$MaxWaitMinutes = 10
)

$ErrorActionPreference = "Stop"

function Write-Step {
    param([string]$Message)
    Write-Host "`n[$(Get-Date -Format 'HH:mm:ss')] === $Message ===" -ForegroundColor Cyan
}

function Write-Info {
    param([string]$Message)
    Write-Host "[$(Get-Date -Format 'HH:mm:ss')] $Message" -ForegroundColor Green
}

function Write-Warn {
    param([string]$Message)
    Write-Host "[$(Get-Date -Format 'HH:mm:ss')] $Message" -ForegroundColor Yellow
}

function Write-Error {
    param([string]$Message)
    Write-Host "[$(Get-Date -Format 'HH:mm:ss')] ERROR: $Message" -ForegroundColor Red
}

# ==================== 1. Push 到 GitHub ====================
Write-Step "Step 1: Push 代码到 GitHub"

try {
    $gitStatus = git status --short 2>$null
    if ([string]::IsNullOrWhiteSpace($gitStatus)) {
        Write-Info "没有未提交的更改，跳过 commit"
    } else {
        Write-Info "发现未提交的更改，自动 add + commit..."
        git add -A
        $commitMsg = Read-Host "请输入 commit 消息 (直接回车使用默认)"
        if ([string]::IsNullOrWhiteSpace($commitMsg)) {
            $commitMsg = "update: $(Get-Date -Format 'yyyy-MM-dd HH:mm:ss')"
        }
        git commit -m "$commitMsg"
    }

    Write-Info "推送到 GitHub..."
    git push origin main
    Write-Info "Push 成功!"
} catch {
    Write-Error "Push 失败: $_"
    exit 1
}

# ==================== 2. 等待 CI 编译 ====================
Write-Step "Step 2: 等待 GitHub Actions 编译完成 (最多 ${MaxWaitMinutes} 分钟)"

$startTime = Get-Date
$runId = $null
$status = ""

while ($true) {
    $elapsed = (Get-Date) - $startTime
    if ($elapsed.TotalMinutes -gt $MaxWaitMinutes) {
        Write-Error "等待超时 (${MaxWaitMinutes} 分钟)，请手动检查: https://github.com/$Repo/actions"
        exit 1
    }

    # 获取最新 run
    $runs = gh run list --repo $Repo --limit 1 --json databaseId,status,conclusion,headSha,createdAt 2>$null | ConvertFrom-Json
    if ($runs -and $runs.Count -gt 0) {
        $latest = $runs[0]
        $runId = $latest.databaseId
        $status = $latest.status
        $conclusion = $latest.conclusion

        Write-Host "  状态: $status$(if($conclusion){" ($conclusion)"}) | 已等待: $($elapsed.ToString('mm\:ss'))" -NoNewline
        Write-Host "`r" -NoNewline

        if ($status -eq "completed") {
            Write-Host "" # 换行
            if ($conclusion -eq "success") {
                Write-Info "编译成功! Run ID: $runId"
                break
            } else {
                Write-Error "编译失败! 结论: $conclusion"
                Write-Info "查看日志: gh run view $runId --repo $Repo --log-failed"
                exit 1
            }
        }
    }

    Start-Sleep -Seconds 10
}

# ==================== 3. 下载固件 ====================
Write-Step "Step 3: 下载编译好的固件"

# 清理旧文件
if (Test-Path $DownloadDir) {
    Remove-Item -Path $DownloadDir -Recurse -Force
}
New-Item -ItemType Directory -Path $DownloadDir -Force | Out-Null

try {
    Write-Info "下载 artifact '$ArtifactName' from run $runId..."
    gh run download $runId --repo $Repo --name $ArtifactName --dir $DownloadDir

    $binFile = Get-ChildItem -Path $DownloadDir -Filter "*.bin" | Select-Object -First 1
    if (-not $binFile) {
        Write-Error "下载成功但未找到 .bin 文件"
        exit 1
    }

    Write-Info "固件下载成功: $($binFile.FullName)"
    Write-Info "文件大小: $([math]::Round($binFile.Length / 1024, 1)) KB"
} catch {
    Write-Error "下载失败: $_"
    exit 1
}

# ==================== 4. 烧录固件 ====================
Write-Step "Step 4: 烧录固件到 ESP32 ($Port)"

# 查找 esptool
$esptoolPaths = @(
    "$env:LOCALAPPDATA\Arduino15\packages\esp32\tools\esptool_py\5.1.0-cn\esptool.exe",
    "$env:LOCALAPPDATA\Arduino15\packages\esp32\tools\esptool_py\*\esptool.exe",
    "esptool.py",
    "esptool"
)

$esptool = $null
foreach ($path in $esptoolPaths) {
    if ($path -like "*\*") {
        $found = Get-Item $path -ErrorAction SilentlyContinue | Select-Object -First 1
        if ($found) {
            $esptool = $found.FullName
            break
        }
    } else {
        $cmd = Get-Command $path -ErrorAction SilentlyContinue
        if ($cmd) {
            $esptool = $cmd.Source
            break
        }
    }
}

if (-not $esptool) {
    Write-Error "找不到 esptool，请确保已安装 Arduino ESP32 支持包或 esptool"
    exit 1
}

Write-Info "使用 esptool: $esptool"

# 检查串口是否可用
$portAvailable = [System.IO.Ports.SerialPort]::GetPortNames() | Where-Object { $_ -eq $Port }
if (-not $portAvailable) {
    Write-Error "串口 $Port 不可用，可用端口: $([System.IO.Ports.SerialPort]::GetPortNames() -join ', ')"
    exit 1
}

# 执行烧录
try {
    Write-Info "开始烧录..."
    & $esptool --chip esp32c3 --port $Port --baud $Baud `
        --before default_reset --after hard_reset `
        write_flash -z --flash_mode dio --flash_freq 80m --flash_size 4MB `
        0x0 "$($binFile.FullName)"

    if ($LASTEXITCODE -eq 0) {
        Write-Info "烧录成功!"
    } else {
        Write-Error "烧录失败，退出码: $LASTEXITCODE"
        exit 1
    }
} catch {
    Write-Error "烧录异常: $_"
    exit 1
}

# ==================== 完成 ====================
Write-Step "全部完成!"
Write-Info "固件已烧录到 $Port"
Write-Info "可以打开串口监视器查看日志"
