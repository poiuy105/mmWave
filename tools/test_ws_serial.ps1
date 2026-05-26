$port = New-Object System.IO.Ports.SerialPort "COM33", 115200, "None", 8, "One"
$port.ReadTimeout = 3000
$port.Open()
Start-Sleep -Milliseconds 200
$port.DiscardInBuffer()
Start-Sleep -Milliseconds 300

Write-Host "--- Monitoring serial (30s), will trigger WS connection at 3s ---"
$buf = ""
$start = Get-Date

# 3 seconds after start, trigger a browser-like request
$triggered = $false

while (((Get-Date) - $start).TotalSeconds -lt 30) {
    $elapsed = ((Get-Date) - $start).TotalSeconds
    
    # At 3s, trigger HTTP and WS requests
    if (-not $triggered -and $elapsed -gt 3) {
        $triggered = $true
        Write-Host "`n--- Triggering HTTP request ---"
        try {
            $r = Invoke-RestMethod -Uri "http://192.168.124.20/api/status" -TimeoutSec 5
            Write-Host "HTTP OK: $($r.server)"
        } catch {
            Write-Host "HTTP FAIL: $_"
        }
        
        Write-Host "--- Triggering WebSocket connection ---"
        try {
            $ws = New-Object System.Net.WebSockets.ClientWebSocket
            $ct = New-Object System.Threading.CancellationTokenSource 5000
            $task = $ws.ConnectAsync("ws://192.168.124.20/ws", $ct.Token)
            $task.Wait()
            Write-Host "WS Connect result: $($task.Status)"
            
            if ($ws.State -eq 'Open') {
                Write-Host "WS OPEN! Waiting for data..."
                $recvBuf = New-Object byte[] 1024
                $seg = New-Object System.ArraySegment[byte] -ArgumentList @(,$recvBuf)
                $recvTask = $ws.ReceiveAsync($seg, $ct.Token)
                $recvTask.Wait(3000)
                Write-Host "WS Receive: $($recvTask.Status), bytes: $($recvTask.Result.Count)"
                
                $ws.CloseAsync([System.Net.WebSockets.WebSocketCloseStatus]::NormalClosure, "ok", $ct.Token).Wait()
            } else {
                Write-Host "WS State: $($ws.State)"
            }
        } catch {
            Write-Host "WS FAIL: $_"
        }
        Write-Host "--- Done triggering ---`n"
    }
    
    if ($port.BytesToRead -gt 0) {
        $data = $port.ReadExisting()
        $buf += $data
        Write-Host $data -NoNewline
    }
    Start-Sleep -Milliseconds 30
}

$port.Close()
Write-Host "`n--- Total: $($buf.Length) chars ---"
