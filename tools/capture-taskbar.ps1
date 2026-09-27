# Saves a PNG of the primary taskbar, or with -All of every monitor's taskbar
# (left to right, stacked top to bottom). Usage: capture-taskbar.ps1 <output.png> [-All]
param([Parameter(Mandatory)] [string] $OutFile, [switch] $All)

Add-Type -AssemblyName System.Drawing
Add-Type @"
using System;
using System.Collections.Generic;
using System.Runtime.InteropServices;
public static class UnglomCapture {
  [StructLayout(LayoutKind.Sequential)] public struct RECT { public int Left, Top, Right, Bottom; }
  [DllImport("user32.dll")] public static extern bool SetProcessDPIAware();
  [DllImport("user32.dll")] public static extern IntPtr FindWindow(string cls, string name);
  [DllImport("user32.dll")] public static extern IntPtr FindWindowEx(IntPtr parent, IntPtr after, string cls, string name);
  [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr hwnd, out RECT rect);
}
"@

[UnglomCapture]::SetProcessDPIAware() | Out-Null
$windows = @([UnglomCapture]::FindWindow("Shell_TrayWnd", $null))
if ($All) {
  $w = [IntPtr]::Zero
  while (($w = [UnglomCapture]::FindWindowEx([IntPtr]::Zero, $w, "Shell_SecondaryTrayWnd", $null)) -ne [IntPtr]::Zero) {
    $windows += $w
  }
}
$rects = foreach ($w in $windows) {
  $rect = New-Object UnglomCapture+RECT
  [UnglomCapture]::GetWindowRect($w, [ref] $rect) | Out-Null
  $rect
}
$rects = @($rects | Sort-Object Left)

[int] $width = ($rects | ForEach-Object { $_.Right - $_.Left } | Measure-Object -Maximum).Maximum
[int] $height = ($rects | ForEach-Object { $_.Bottom - $_.Top } | Measure-Object -Sum).Sum
$bitmap = New-Object System.Drawing.Bitmap $width, $height
$graphics = [System.Drawing.Graphics]::FromImage($bitmap)
$y = 0
foreach ($rect in $rects) {
  $size = New-Object System.Drawing.Size ($rect.Right - $rect.Left), ($rect.Bottom - $rect.Top)
  $graphics.CopyFromScreen($rect.Left, $rect.Top, 0, $y, $size)
  $y += $size.Height
}
$bitmap.Save($OutFile, [System.Drawing.Imaging.ImageFormat]::Png)
$graphics.Dispose()
$bitmap.Dispose()
"Saved ${width}x${height} image of $($rects.Count) taskbar(s) to $OutFile"
