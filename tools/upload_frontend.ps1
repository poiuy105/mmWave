# 上传所有 frontend 文件到 ESP32
$baseUrl = "http://192.168.124.20/api/files/upload"
$frontendDir = "e:\Espidf\mmWave\frontend"
$files = @("index.html", "style.css", "websocket.js", "api.js", "radar.js", "canvas.js", "main.js")

$webClient = New-Object System.Net.WebClient

foreach ($file in $files) {
    $filePath = Join-Path $frontendDir $file
    if (Test-Path $filePath) {
        $url = "$baseUrl?path=/storage/www/$file"
        Write-Host "Uploading $file..." -NoNewline
        try {
            $response = $webClient.UploadFile($url, $filePath)
            $json = [System.Text.Encoding]::UTF8.GetString($response) | ConvertFrom-Json
            if ($json.success) {
                Write-Host " OK (size: $($json.size))" -ForegroundColor Green
            } else {
                Write-Host " FAILED" -ForegroundColor Red
            }
        } catch {
            Write-Host " ERROR: $_" -ForegroundColor Red
        }
    } else {
        Write-Host "File not found: $filePath" -ForegroundColor Yellow
    }
}

Write-Host ""
Write-Host "Verifying files..."
$response = Invoke-RestMethod -Uri "http://192.168.124.20/api/files/list?path=/storage/www"
$response.files | Format-Table name, size -AutoSize
