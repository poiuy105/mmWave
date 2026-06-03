#requires -Version 5.1
[Console]::OutputEncoding = [System.Text.Encoding]::UTF8
$PSDefaultParameterValues['Out-File:Encoding'] = 'utf8'

<#
.SYNOPSIS
    One-click build and flash ESP32 firmware
.DESCRIPTION
    1. Push code to GitHub
    2. Wait for GitHub Actions build
    3. Download merged.bin
    4. Flash to ESP32
.NOTES
    Requires: git, gh CLI, esptool
    Need gh login: gh auth login
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

function Write-Err {
    param([string]$Message)
    Write-Host "[$(Get-Date -Format 'HH:mm:ss')] ERROR: $Message" -ForegroundColor Red
}

# ==================== Step 1: Push to GitHub ====================
Write-Step "Step 1: Push to GitHub"

try {
    $gitStatus = git status --short 2>$null
    if ([string]::IsNullOrWhiteSpace($gitStatus)) {
        Write-Info "No uncommitted changes, skip commit"
    } else {
        Write-Info "Found uncommitted changes, auto add + commit..."
        git add -A
        $commitMsg = Read-Host "Enter commit message (press Enter for default)"
        if ([string]::IsNullOrWhiteSpace($commitMsg)) {
            $commitMsg = "update: $(Get-Date -Format 'yyyy-MM-dd HH:mm:ss')"
        }
        git commit -m "$commitMsg"
    }

    Write-Info "Pushing to GitHub..."
    git push origin main
    Write-Info "Push success!"
} catch {
    Write-Err "Push failed: $_"
    exit 1
}

# ==================== Step 2: Wait for CI ====================
Write-Step "Step 2: Wait for GitHub Actions build (max ${MaxWaitMinutes} min)"

$startTime = Get-Date
$runId = $null
$status = ""

while ($true) {
    $elapsed = (Get-Date) - $startTime
    if ($elapsed.TotalMinutes -gt $MaxWaitMinutes) {
        Write-Err "Timeout (${MaxWaitMinutes} min), check manually: https://github.com/$Repo/actions"
        exit 1
    }

    $runs = gh run list --repo $Repo --limit 1 --json databaseId,status,conclusion,headSha,createdAt 2>$null | ConvertFrom-Json
    if ($runs -and $runs.Count -gt 0) {
        $latest = $runs[0]
        $runId = $latest.databaseId
        $status = $latest.status
        $conclusion = $latest.conclusion

        Write-Host "  Status: $status$(if($conclusion){" ($conclusion)"}) | Elapsed: $($elapsed.ToString('mm\:ss'))" -NoNewline
        Write-Host "`r" -NoNewline

        if ($status -eq "completed") {
            Write-Host ""
            if ($conclusion -eq "success") {
                Write-Info "Build success! Run ID: $runId"
                break
            } else {
                Write-Err "Build failed! Conclusion: $conclusion"
                Write-Info "View log: gh run view $runId --repo $Repo --log-failed"
                exit 1
            }
        }
    }

    Start-Sleep -Seconds 10
}

# ==================== Step 3: Download Firmware ====================
Write-Step "Step 3: Download firmware"

if (Test-Path $DownloadDir) {
    Remove-Item -Path $DownloadDir -Recurse -Force
}
New-Item -ItemType Directory -Path $DownloadDir -Force | Out-Null

try {
    Write-Info "Downloading artifact '$ArtifactName' from run $runId..."
    gh run download $runId --repo $Repo --name $ArtifactName --dir $DownloadDir

    $binFile = Get-ChildItem -Path $DownloadDir -Filter "*.bin" | Select-Object -First 1
    if (-not $binFile) {
        Write-Err "Download success but no .bin file found"
        exit 1
    }

    Write-Info "Firmware downloaded: $($binFile.FullName)"
    Write-Info "File size: $([math]::Round($binFile.Length / 1024, 1)) KB"
} catch {
    Write-Err "Download failed: $_"
    exit 1
}

# ==================== Step 4: Flash Firmware ====================
Write-Step "Step 4: Flash firmware to ESP32 ($Port)"

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
    Write-Err "esptool not found, please install Arduino ESP32 package or esptool"
    exit 1
}

Write-Info "Using esptool: $esptool"

$portAvailable = [System.IO.Ports.SerialPort]::GetPortNames() | Where-Object { $_ -eq $Port }
if (-not $portAvailable) {
    Write-Err "Port $Port not available, available ports: $([System.IO.Ports.SerialPort]::GetPortNames() -join ', ')"
    exit 1
}

try {
    Write-Info "Starting flash..."
    & $esptool --chip esp32c3 --port $Port --baud $Baud `
        --before default_reset --after hard_reset `
        write_flash -z --flash_mode dio --flash_freq 80m --flash_size 4MB `
        0x0 "$($binFile.FullName)"

    if ($LASTEXITCODE -eq 0) {
        Write-Info "Flash success!"
    } else {
        Write-Err "Flash failed, exit code: $LASTEXITCODE"
        exit 1
    }
} catch {
    Write-Err "Flash exception: $_"
    exit 1
}

# ==================== Done ====================
Write-Step "All done!"
Write-Info "Firmware flashed to $Port"
Write-Info "Open serial monitor to view logs"
