param([string]$Port)
$ports = [System.IO.Ports.SerialPort]::GetPortNames()
Write-Host ('Puertos COM disponibles: ' + ($ports -join ', '))
if (-not $Port) {
    if ($ports.Count -eq 1) { $Port = $ports[0] }
    else { throw 'Indica -Port COMx. No se ha seleccionado ningun puerto automaticamente.' }
}
if ($Port -notin $ports) { throw "No existe $Port. Comprueba que esta cargado pico_8bitdo_cdc_diagnostic.uf2." }
$serial = [System.IO.Ports.SerialPort]::new($Port, 115200)
$serial.ReadTimeout = 1000
$serial.DtrEnable = $true
$log = Join-Path (Get-Location) ('8bitdo-' + (Get-Date -Format 'yyyyMMdd-HHmmss') + '.log')
try {
    $serial.Open()
    $phases = @(
        @('Pair the controller and leave it idle', 15),
        @('REPOSO', 4),
        @('Press the A label printed on the controller', 4),
        @('REPOSO', 2),
        @('Press the B label printed on the controller', 4),
        @('REPOSO', 2),
        @('Press the X label printed on the controller', 4),
        @('REPOSO', 2),
        @('Press the Y label printed on the controller', 4),
        @('REPOSO', 2),
        @('Move the D-pad through all four cardinal directions', 6),
        @('REPOSO', 2),
        @('Press LB and RB separately', 6),
        @('REPOSO', 2),
        @('Press LT and RT separately', 6),
        @('REPOSO', 2),
        @('Press each stick click separately', 6),
        @('REPOSO', 2),
        @('Move only the left stick', 6),
        @('REPOSO', 2),
        @('Move only the right stick', 6),
        @('REPOSO final', 4)
    )
    Write-Host "Captura guiada en $log"
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
Write-Host "`nLog guardado: $log"
