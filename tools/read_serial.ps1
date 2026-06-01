$port = New-Object System.IO.Ports.SerialPort "COM33", 115200, "None", 8, "One"
$port.ReadTimeout = 3000
$port.Open()
Start-Sleep -Milliseconds 200
$port.DiscardInBuffer()
Start-Sleep -Milliseconds 300
Write-Host "--- Waiting for browser connection (20s) ---"
$buf = ""
$start = Get-Date
while (((Get-Date) - $start).TotalSeconds -lt 20) {
    if ($port.BytesToRead -gt 0) {
        $data = $port.ReadExisting()
        $buf += $data
        Write-Host $data -NoNewline
    }
    Start-Sleep -Milliseconds 30
}
$port.Close()
Write-Host ""
Write-Host "--- Total: $($buf.Length) chars ---"
