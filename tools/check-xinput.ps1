param([int]$Seconds = 30, [switch]$RequireTwo)
Add-Type @'
using System;
using System.Runtime.InteropServices;
[StructLayout(LayoutKind.Sequential, Pack=1)] public struct XPad {
  public ushort buttons; public byte lt, rt;
  public short lx, ly, rx, ry;
}
[StructLayout(LayoutKind.Sequential, Pack=1)] public struct XState {
  public uint packet; public XPad pad;
}
public static class XInputProbe {
  [DllImport("xinput1_4.dll", EntryPoint="XInputGetState")]
  public static extern uint GetState(uint index, out XState state);
}
'@
$last = @{}
$buttonNames = [ordered]@{
  0x0001='DPadUp'; 0x0002='DPadDown'; 0x0004='DPadLeft'; 0x0008='DPadRight'
  0x0010='Menu'; 0x0020='View'; 0x0040='L3'; 0x0080='R3'
  0x0100='LB'; 0x0200='RB'; 0x0400='Guide'
  0x1000='A'; 0x2000='B'; 0x4000='X'; 0x8000='Y'
}
$until = (Get-Date).AddSeconds($Seconds)
$maximumConnected = 0
Write-Host 'Checking XInput indexes 0..3; other controllers may already be connected.'
while ((Get-Date) -lt $until) {
  $connectedThisPoll = 0
  for ($i = 0; $i -lt 4; $i++) {
    $state = [XState]::new()
    $result = [XInputProbe]::GetState([uint32]$i, [ref]$state)
    $line = if ($result -eq 0) {
      $connectedThisPoll++
      $pressed = @($buttonNames.Keys | Where-Object { ($state.pad.buttons -band [int]$_) -ne 0 } | ForEach-Object { $buttonNames[$_] })
      ('connected packet={0} buttons=0x{1:X4} [{8}] LT={2} RT={3} LX={4} LY={5} RX={6} RY={7}' -f
       $state.packet,$state.pad.buttons,$state.pad.lt,$state.pad.rt,
       $state.pad.lx,$state.pad.ly,$state.pad.rx,$state.pad.ry,($pressed -join ','))
    } else { "disconnected error=$result" }
    if ($last[$i] -ne $line) {
      '{0:o} windows_index={1} {2}' -f (Get-Date),$i,$line
      $last[$i] = $line
    }
  }
  if ($connectedThisPoll -gt $maximumConnected) { $maximumConnected = $connectedThisPoll }
  Start-Sleep -Milliseconds 100
}
Write-Host "Maximum connected XInput indices observed: $maximumConnected"
if ($RequireTwo -and $maximumConnected -lt 2) {
  Write-Error 'Two XInput indexes were expected, but Windows did not expose them during this capture.'
  exit 1
}
