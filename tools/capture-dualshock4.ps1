param(
  [Parameter(Mandatory=$true)][string]$Port,
  [string]$Output = (Join-Path $PSScriptRoot ("ds4-capture-{0:yyyyMMdd-HHmmss}.log" -f (Get-Date))),
  [switch]$RestOnly,
  [ValidateRange(1,32)][int]$RestSamples = 24
)
$ErrorActionPreference = 'Stop'
$serial = [System.IO.Ports.SerialPort]::new($Port, 115200, 'None', 8, 'One')
$serial.ReadTimeout = 1000
$serial.NewLine = "`n"
$serial.Open()
Write-Host "Capturing Pico CDC diagnostics from $Port to $Output"
Write-Host 'Waiting for a descriptor-confirmed DS4 and a valid controls report.'
Write-Host 'Do not start the button sequence before INPUT_READY appears.'
$ready = $false
$writer = [System.IO.StreamWriter]::new($Output, $false, [System.Text.Encoding]::UTF8)
try {
  while (-not $ready) {
    try { $line = $serial.ReadLine().TrimEnd("`r") }
    catch [TimeoutException] { continue }
    $writer.WriteLine($line); $writer.Flush(); Write-Host $line
    if ($line -match '\[DS4-INPUT\] INPUT_READY') { $ready = $true }
    if ($line -match '\[CLASSIC-DIAG\].*model=sony-unconfirmed') {
      Write-Warning 'The name matched but the HID descriptor did not confirm DS4. No controls test will start.'
    }
  }
  if ($RestOnly) {
    Write-Host "Leave the DS4 completely untouched; requesting $RestSamples correlated raw/parsed samples."
    $serial.WriteLine("DS4 CAPTURE $RestSamples")
    $until = [DateTime]::UtcNow.AddSeconds([Math]::Max(30, $RestSamples * 2))
    $complete = $false
    while ([DateTime]::UtcNow -lt $until -and -not $complete) {
      try { $line = $serial.ReadLine().TrimEnd("`r") }
      catch [TimeoutException] { continue }
      $writer.WriteLine($line); $writer.Flush(); Write-Host $line
      if ($line -match '\[DS4-CAPTURE\] complete') { $complete = $true }
    }
    if (-not $complete) { Write-Warning 'Capture did not report completion before timeout; the partial log is preserved.' }
    return
  }
  $actions = @(
    'Rest / release all controls', 'Cross', 'Circle', 'Square', 'Triangle',
    'D-pad Up', 'D-pad Right', 'D-pad Down', 'D-pad Left',
    'L1', 'R1', 'L2', 'R2', 'L3', 'R3',
    'Left stick center', 'Left stick left', 'Left stick right', 'Left stick up', 'Left stick down',
    'Right stick center', 'Right stick left', 'Right stick right', 'Right stick up', 'Right stick down',
    'SHARE', 'OPTIONS'
  )
  foreach ($action in $actions) {
    [void](Read-Host "Press Enter when ready to perform '$action'")
    Write-Host "Perform '$action' now. Hold it for about 2 seconds."
    $until = [DateTime]::UtcNow.AddSeconds(3)
    while ([DateTime]::UtcNow -lt $until) {
      try { $line = $serial.ReadLine().TrimEnd("`r"); $writer.WriteLine($line); $writer.Flush(); Write-Host $line }
      catch [TimeoutException] { }
    }
  }
} finally {
  $writer.Dispose(); $serial.Dispose()
  Write-Host "Log saved: $Output"
}
