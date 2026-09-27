# Sends a tray-menu command to the running Unglom.exe, for testing.
# Commands: 1 = Pause/Resume, 2 = Start with Windows, 3 = Open log folder, 4 = Exit
param([Parameter(Mandatory)] [int] $Command)

Add-Type @"
using System;
using System.Runtime.InteropServices;
public static class UnglomMenu {
  [DllImport("user32.dll", CharSet = CharSet.Unicode)] public static extern IntPtr FindWindowW(string cls, string name);
  [DllImport("user32.dll")] public static extern bool PostMessageW(IntPtr hwnd, uint msg, IntPtr wparam, IntPtr lparam);
}
"@

$hwnd = [UnglomMenu]::FindWindowW("UnglomLoaderWindow", [NullString]::Value)
if ($hwnd -eq [IntPtr]::Zero) { "Unglom.exe is not running"; exit 1 }
[UnglomMenu]::PostMessageW($hwnd, 0x0111, [IntPtr]$Command, [IntPtr]::Zero) | Out-Null
"Sent command $Command"
