Add-Type @"
using System;
using System.Runtime.InteropServices;
public class T {
  [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern IntPtr FindWindowEx(IntPtr parent, IntPtr after, string cls, string title);
  [DllImport("user32.dll")] public static extern bool PostMessage(IntPtr h, uint msg, IntPtr w, IntPtr l);
  [DllImport("user32.dll")] public static extern void keybd_event(byte vk, byte scan, uint flags, IntPtr extra);
  [DllImport("user32.dll")] public static extern bool SetCursorPos(int x, int y);
}
"@
$hwndMessage = [IntPtr](-3)
$out = Join-Path $env:TEMP "menu.png"
$h = [T]::FindWindowEx($hwndMessage, [IntPtr]::Zero, "ArcTrayWnd", $null)
if ($h -eq [IntPtr]::Zero) {
  # message-only windows are not reachable from FindWindow: take the HWND the app logged
  $log = Join-Path $env:TEMP "arctray.log"
  $line = (Get-Content $log | Select-String "window created hwnd=" | Select-Object -Last 1).ToString()
  $hex = ($line -split "hwnd=")[1].Trim()
  $h = [IntPtr]([Convert]::ToInt64($hex, 16))
  Write-Output "hwnd from log: $hex"
}
# put the cursor near the bottom-right so the menu opens in a sane place
[T]::SetCursorPos(1700, 900) | Out-Null
Start-Sleep -Milliseconds 150
# WM_APP+1 (WM_TRAY), wParam = tray id, lParam = WM_RBUTTONUP
[T]::PostMessage($h, 0x8001, [IntPtr]1, [IntPtr]0x0205) | Out-Null
Start-Sleep -Milliseconds 900
Add-Type -AssemblyName System.Windows.Forms,System.Drawing
$b = [System.Windows.Forms.Screen]::PrimaryScreen.Bounds
$bmp = New-Object System.Drawing.Bitmap $b.Width, $b.Height
$g = [System.Drawing.Graphics]::FromImage($bmp)
$g.CopyFromScreen($b.X, $b.Y, 0, 0, $bmp.Size)
$bmp.Save($out, [System.Drawing.Imaging.ImageFormat]::Png)
$g.Dispose(); $bmp.Dispose()
# memory while the menu is open
$p = Get-Process arctray
Write-Output ("menu-open: WS={0:N1} MB private={1:N1} MB" -f ($p.WorkingSet64/1MB), ($p.PrivateMemorySize64/1MB))
# close the popup
[T]::keybd_event(0x1B, 0, 0, [IntPtr]::Zero)
[T]::keybd_event(0x1B, 0, 2, [IntPtr]::Zero)
Start-Sleep -Milliseconds 400
Write-Output $out
