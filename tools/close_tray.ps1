Add-Type @"
using System;
using System.Runtime.InteropServices;
public class W {
  [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern IntPtr FindWindow(string cls, string title);
  [DllImport("user32.dll")] public static extern bool PostMessage(IntPtr h, uint msg, IntPtr w, IntPtr l);
}
"@
$h = [W]::FindWindow("ArcTrayWnd", $null)
if ($h -eq [IntPtr]::Zero) { Write-Output "window not found"; exit 1 }
[W]::PostMessage($h, 0x10, [IntPtr]::Zero, [IntPtr]::Zero) | Out-Null
Write-Output "posted WM_CLOSE to $h"
