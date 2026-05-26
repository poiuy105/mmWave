$port = New-Object System.IO.Ports.SerialPort "COM33", 115200, "None", 8, "One"
$port.ReadTimeout = 10000
try {
    $port.Open()
    Write-Host "Port opened"
    Start-Sleep -Milliseconds 200
    $port.DiscardInBuffer()
    Start-Sleep -Milliseconds 500

    $buffer = ""
    $start = Get-Date
    while (((Get-Date) - $start).TotalSeconds -lt 8) {
        if ($port.BytesToRead -gt 0) {
            $data = $port.ReadExisting()
            $buffer += $data
            Write-Host $data -NoNewline
        }
        Start-Sleep -Milliseconds 50
    }
    Write-Host ""
    Write-Host "--- Buffer ($($buffer.Length) chars) ---"
} finally {
    if ($port.IsOpen) { $port.Close() }
}
