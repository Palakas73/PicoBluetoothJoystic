param(
  [ValidateRange(2,300)][int]$Seconds = 15,
  [ValidateRange(0,20)][int]$SleepMs = 0
)
Add-Type @'
using System;
using System.Runtime.InteropServices;
[StructLayout(LayoutKind.Sequential, Pack=1)] public struct AFPad {
  public ushort buttons; public byte lt, rt; public short lx, ly, rx, ry;
}
[StructLayout(LayoutKind.Sequential, Pack=1)] public struct AFState {
  public uint packet; public AFPad pad;
}
public static class AFProbe {
  [DllImport("xinput1_4.dll", EntryPoint="XInputGetState")]
  public static extern uint GetState(uint index, out AFState state);
}
'@
$names = [ordered]@{
  0x0001='DPadUp';0x0002='DPadDown';0x0004='DPadLeft';0x0008='DPadRight'
  0x0010='Menu';0x0020='View';0x0040='L3';0x0080='R3';0x0100='LB';0x0200='RB'
  0x0400='Guide';0x1000='A';0x2000='B';0x4000='X';0x8000='Y'
}
$watch = [Diagnostics.Stopwatch]::StartNew()
$endMs = $Seconds * 1000.0
$lastPoll = 0.0; $intervalTotal = 0.0; $intervalMin = [double]::MaxValue; $intervalMax = 0.0
$polls = 0L; $last = @{}; $transitions = @{}
Write-Host "High-rate XInput sampling for $Seconds s. This measures Windows polling observations, not physical end-to-end latency."
while ($watch.Elapsed.TotalMilliseconds -lt $endMs) {
  $now = $watch.Elapsed.TotalMilliseconds
  if ($polls -gt 0) {
    $dt = $now - $lastPoll; $intervalTotal += $dt
    if ($dt -lt $intervalMin) { $intervalMin = $dt }
    if ($dt -gt $intervalMax) { $intervalMax = $dt }
  }
  $lastPoll = $now; $polls++
  for ($i=0; $i -lt 4; $i++) {
    $s=[AFState]::new(); $rc=[AFProbe]::GetState([uint32]$i,[ref]$s)
    if ($rc -ne 0) { continue }
    foreach ($mask in $names.Keys) {
      $key="$i/$($names[$mask])"; $on=($s.pad.buttons -band [int]$mask) -ne 0
      if ($last.ContainsKey($key) -and $last[$key] -ne $on) {
        if (!$transitions.ContainsKey($key)) { $transitions[$key]=0 }
        $transitions[$key]++
        '{0,10:N3} ms index={1} {2}={3}' -f $now,$i,$names[$mask],$(if($on){'ON'}else{'OFF'})
      }
      $last[$key]=$on
    }
    foreach ($pair in @(@('LT',[int]$s.pad.lt),@('RT',[int]$s.pad.rt))) {
      $key="$i/$($pair[0])"; $on=$pair[1] -ge 128
      if ($last.ContainsKey($key) -and $last[$key] -ne $on) {
        if (!$transitions.ContainsKey($key)) { $transitions[$key]=0 }
        $transitions[$key]++
        '{0,10:N3} ms index={1} {2}={3} value={4}' -f $now,$i,$pair[0],$(if($on){'ON'}else{'OFF'}),$pair[1]
      }
      $last[$key]=$on
    }
  }
  if ($SleepMs -gt 0) { [Threading.Thread]::Sleep($SleepMs) }
}
$elapsed=$watch.Elapsed.TotalSeconds; $rate=$polls/$elapsed
$avg=if($polls -gt 1){$intervalTotal/($polls-1)}else{0}
Write-Host ('Polling: {0:N1} samples/s, interval avg={1:N3} ms min={2:N3} ms max={3:N3} ms' -f $rate,$avg,$intervalMin,$intervalMax)
foreach($key in ($transitions.Keys | Sort-Object)) {
  $cycles=$transitions[$key]/2.0
  Write-Host ('{0}: transitions={1}, approximate full cycles/s over capture={2:N2}' -f $key,$transitions[$key],($cycles/$elapsed))
}
if($rate -lt 200) { Write-Warning 'Sampling was below 200 Hz; 20 Hz Autofire transitions may be missed. Close busy applications or use -SleepMs 0.' }
Write-Host 'A missed Windows sample is not firmware latency evidence; this tool does not measure Bluetooth-to-display end-to-end latency.'
