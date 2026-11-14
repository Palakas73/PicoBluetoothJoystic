param([string]$Port, [string]$ControllerLabel = '8BitDo Ultimate 2C',
      [string]$LogPrefix = '8bitdo', [int]$ReadyTimeoutSeconds = 75)
$ports = [System.IO.Ports.SerialPort]::GetPortNames()
Write-Host ('Available COM ports: ' + ($ports -join ', '))
if (-not $Port) {
    if ($ports.Count -eq 1) { $Port = $ports[0] }
    else { throw 'Specify -Port COMx. No port was selected automatically.' }
}
if ($Port -notin $ports) { throw "$Port does not exist. Load the CDC diagnostic firmware first." }
$serial = [System.IO.Ports.SerialPort]::new($Port, 115200)
$serial.ReadTimeout = 1000
$serial.DtrEnable = $true
$log = Join-Path (Get-Location) ($LogPrefix + '-' + (Get-Date -Format 'yyyyMMdd-HHmmss') + '.log')
try {
    $serial.Open()
    Write-Host "Waiting up to $ReadyTimeoutSeconds seconds for an interpreted control report..."
    $ready = $false
    $preflight = [System.Text.StringBuilder]::new()
    $deadline = (Get-Date).AddSeconds($ReadyTimeoutSeconds)
    $marker = "`r`n[HOST-ACTION] $(Get-Date -Format o) Pair $ControllerLabel and leave it idle`r`n"
    [System.IO.File]::AppendAllText($log, $marker)
    Write-Host $marker
    while ((Get-Date) -lt $deadline -and -not $ready) {
        $data = $serial.ReadExisting()
        if ($data.Length) {
            [System.IO.File]::AppendAllText($log, $data)
            Write-Host -NoNewline $data
            [void]$preflight.Append($data)
            if ($preflight.Length -gt 32768) {
                [void]$preflight.Remove(0, $preflight.Length - 16384)
            }
            $text = $preflight.ToString()
            $ready = $text -match '\[SWITCH-STATE\]' -or
                     $text -match '\[SWITCH-IDENTITY\].*accepted' -or
                     $text -match '\[SWITCH-DIAG\].*ok=[1-9][0-9]*'
        }
        Start-Sleep -Milliseconds 50
    }
    if (-not $ready) {
        $failure = "`r`n[HOST-RESULT] $(Get-Date -Format o) INPUT_READY not observed; guided controls skipped.`r`n"
        [System.IO.File]::AppendAllText($log, $failure)
        Write-Host $failure -ForegroundColor Red
        $diagnostic = ($preflight.ToString() -split "`r?`n" |
            Where-Object { $_ -match '\[(BT-CLASSIC|SWITCH-DIAG|SWITCH-INIT)\]' } |
            Select-Object -Last 15) -join [Environment]::NewLine
        if ($diagnostic) { Write-Host "Last connection diagnostics:`n$diagnostic" }
        throw "Controller input was not ready. Connection log preserved at $log"
    }
    $readyMarker = "`r`n[HOST-RESULT] $(Get-Date -Format o) INPUT_READY observed; starting guided controls.`r`n"
    [System.IO.File]::AppendAllText($log, $readyMarker)
    Write-Host $readyMarker -ForegroundColor Green
    $phases = @(
        @('IDLE', 4),
        @('Press the A label printed on the controller', 4),
        @('IDLE', 2),
        @('Press the B label printed on the controller', 4),
        @('IDLE', 2),
        @('Press the X label printed on the controller', 4),
        @('IDLE', 2),
        @('Press the Y label printed on the controller', 4),
        @('IDLE', 2),
        @('Move the D-pad through all four cardinal directions', 6),
        @('IDLE', 2),
        @('Press LB and RB separately', 6),
        @('IDLE', 2),
        @('Press LT and RT separately', 6),
        @('IDLE', 2),
        @('Press each stick click separately', 6),
        @('IDLE', 2),
        @('Move only the left stick', 6),
        @('IDLE', 2),
        @('Move only the right stick', 6),
        @('FINAL IDLE', 4)
    )
    Write-Host "Guided capture: $log"
    foreach ($phase in $phases) {
        $marker = "`r`n[HOST-ACTION] $(Get-Date -Format o) $($phase[0])`r`n"
        [System.IO.File]::AppendAllText($log, $marker)
        Write-Host $marker
        $until = (Get-Date).AddSeconds([int]$phase[1])
        while ((Get-Date) -lt $until) {
            try {
                $data = $serial.ReadExisting()
                if ($data.Length) {
                    [System.IO.File]::AppendAllText($log, $data)
                    Write-Host -NoNewline $data
                }
            } catch [System.TimeoutException] {}
            Start-Sleep -Milliseconds 100
        }
    }
} finally {
    if ($serial.IsOpen) { $serial.Close() }
}
Write-Host "`nLog saved: $log"
