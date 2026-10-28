Add-Type @"
using System;
using System.Text;
using System.Runtime.InteropServices;
public class E {
  public delegate bool EnumProc(IntPtr h, IntPtr l);
  [DllImport("user32.dll")] public static extern bool EnumChildWindows(IntPtr parent, EnumProc cb, IntPtr l);
  [DllImport("user32.dll")] public static extern int GetWindowThreadProcessId(IntPtr h, out uint pid);
  [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern int GetClassName(IntPtr h, StringBuilder sb, int max);
  [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern int GetWindowTextW(IntPtr h, StringBuilder sb, int max);
}
"@
$target = (Get-Process arctray -ErrorAction SilentlyContinue | Select-Object -First 1).Id
Write-Output "pid=$target"
$found = @()
$cb = [E+EnumProc]{
  param($h, $l)
  $p = 0
  [E]::GetWindowThreadProcessId($h, [ref]$p) | Out-Null
  if ($p -eq $target) {
    $sb = New-Object System.Text.StringBuilder 256
    [E]::GetClassName($h, $sb, 256) | Out-Null
    $tt = New-Object System.Text.StringBuilder 256
    [E]::GetWindowTextW($h, $tt, 256) | Out-Null
    $script:found += [pscustomobject]@{ Handle = $h; Class = $sb.ToString(); Title = $tt.ToString() }
  }
  return $true
}
[E]::EnumChildWindows([IntPtr](-3), $cb, [IntPtr]::Zero) | Out-Null
$found | Format-Table -Auto
