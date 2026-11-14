param([string]$Port, [int]$ReadyTimeoutSeconds = 75,
      [int]$ReconnectObservationSeconds = 45)

$ports = [System.IO.Ports.SerialPort]::GetPortNames()
Write-Host ('Available COM ports: ' + ($ports -join ', '))
if (-not $Port) {
    if ($ports.Count -eq 1) { $Port = $ports[0] }
    else { throw 'Specify -Port COMx. No port was selected automatically.' }
}
if ($Port -notin $ports) { throw "$Port does not exist. Load the CDC diagnostic firmware first." }

$serial = [System.IO.Ports.SerialPort]::new($Port, 115200)
$serial.ReadTimeout = 500
$serial.DtrEnable = $true
$log = Join-Path (Get-Location) ('switch-pro-reconnect-' +
    (Get-Date -Format 'yyyyMMdd-HHmmss') + '.log')
$buffer = [System.Text.StringBuilder]::new()

function Read-Diagnostic([int]$Milliseconds) {
    $until = (Get-Date).AddMilliseconds($Milliseconds)
    while ((Get-Date) -lt $until) {
        $data = $serial.ReadExisting()
        if ($data.Length) {
            [System.IO.File]::AppendAllText($log, $data)
            Write-Host -NoNewline $data
            [void]$buffer.Append($data)
            if ($buffer.Length -gt 65536) {
                [void]$buffer.Remove(0, $buffer.Length - 32768)
            }
        }
        Start-Sleep -Milliseconds 50
    }
}

function Add-Marker([string]$Text) {
    $marker = "`r`n[HOST-ACTION] $(Get-Date -Format o) $Text`r`n"
    [System.IO.File]::AppendAllText($log, $marker)
    Write-Host $marker -ForegroundColor Cyan
}

try {
    $serial.Open()
    Add-Marker 'Reconnect test started. Do NOT use Pair.'
    Write-Host "Waiting for the already paired controller to reach INPUT_READY..."
    $deadline = (Get-Date).AddSeconds($ReadyTimeoutSeconds)
    $ready = $false
    while ((Get-Date) -lt $deadline -and -not $ready) {
        Read-Diagnostic 200
        $text = $buffer.ToString()
        $ready = $text -match '\[SWITCH-IDENTITY\].*accepted' -or
                 $text -match '\[SWITCH-DIAG\].*ok=[1-9][0-9]*'
    }
    if (-not $ready) {
        Add-Marker 'INPUT_READY was not observed; reconnect test aborted.'
        throw "Initial controller connection was not ready. Log preserved at $log"
    }

    Add-Marker 'Briefly press SYNC once now to disconnect the Switch Pro.'
    Write-Host 'Do not hold SYNC and do not enter Pair mode.' -ForegroundColor Yellow
    Read-Diagnostic 10000
    Add-Marker 'Now press A once. Waiting for normal bonded reconnection.'
    $beforeReconnect = $buffer.Length
    Read-Diagnostic ($ReconnectObservationSeconds * 1000)

    $observed = $buffer.ToString()
    $disconnected = $observed -match 'HID disconnected|ACL disconnected'
    $tail = if ($beforeReconnect -lt $observed.Length) {
        $observed.Substring($beforeReconnect)
    } else { '' }
    $reconnected = $tail -match 'HID connected' -and
                   ($tail -match '\[SWITCH-IDENTITY\].*accepted' -or
                    $tail -match '\[SWITCH-STATE\]')
    $result = "disconnected=$disconnected reconnected_without_pair=$reconnected"
    Add-Marker "Reconnect observation complete: $result"
    if (-not $reconnected) {
        Write-Host "Normal reconnection was not confirmed. Keep this log: $log" -ForegroundColor Red
        exit 1
    }
    Write-Host "Normal reconnection observed. Log: $log" -ForegroundColor Green
} finally {
    if ($serial.IsOpen) { $serial.Close() }
}
