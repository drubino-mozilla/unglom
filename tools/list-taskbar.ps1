# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

# Lists every taskbar's visible buttons: windows with their AppUserModelID and
# title, and pinned apps that aren't running.
Add-Type -AssemblyName UIAutomationClient, UIAutomationTypes
Add-Type @"
using System;
using System.Runtime.InteropServices;
using System.Text;
public static class UnglomProbe {
  [DllImport("shell32.dll")] static extern int SHGetPropertyStoreForWindow(IntPtr hwnd, ref Guid iid, [MarshalAs(UnmanagedType.Interface)] out IPropertyStore store);
  [DllImport("user32.dll", CharSet = CharSet.Unicode)] static extern int GetWindowTextW(IntPtr hwnd, StringBuilder sb, int max);
  [ComImport, Guid("886D8EEB-8CF2-4446-8D02-CDBA1DBDCF99"), InterfaceType(ComInterfaceType.InterfaceIsIUnknown)]
  interface IPropertyStore { int GetCount(out uint c); int GetAt(uint i, out PROPERTYKEY k); int GetValue(ref PROPERTYKEY k, [Out] PROPVARIANT v); }
  [StructLayout(LayoutKind.Sequential)] public struct PROPERTYKEY { public Guid fmtid; public uint pid; }
  [StructLayout(LayoutKind.Sequential)] public class PROPVARIANT { public ushort vt; ushort a, b, c; public IntPtr p; public IntPtr p2; }
  public static string Aumid(IntPtr hwnd) {
    Guid iid = new Guid("886D8EEB-8CF2-4446-8D02-CDBA1DBDCF99");
    IPropertyStore store;
    if (SHGetPropertyStoreForWindow(hwnd, ref iid, out store) != 0) return "";
    var key = new PROPERTYKEY { fmtid = new Guid("9F4C2855-9F79-4B39-A8D0-E1D42DE1D5F3"), pid = 5 };
    var value = new PROPVARIANT();
    store.GetValue(ref key, value);
    return value.vt == 31 ? Marshal.PtrToStringUni(value.p) : "";
  }
  public static string Title(IntPtr hwnd) {
    var sb = new StringBuilder(512);
    GetWindowTextW(hwnd, sb, 512);
    return sb.ToString();
  }
}
"@

$root = [Windows.Automation.AutomationElement]::RootElement
$buttonCondition = New-Object Windows.Automation.PropertyCondition(
  [Windows.Automation.AutomationElement]::ClassNameProperty, 'Taskbar.TaskListButtonAutomationPeer')
foreach ($class in 'Shell_TrayWnd', 'Shell_SecondaryTrayWnd') {
  $trayCondition = New-Object Windows.Automation.PropertyCondition([Windows.Automation.AutomationElement]::ClassNameProperty, $class)
  foreach ($tray in $root.FindAll([Windows.Automation.TreeScope]::Children, $trayCondition)) {
    "=== $class at x=$($tray.Current.BoundingRectangle.X)"
    foreach ($e in $tray.FindAll([Windows.Automation.TreeScope]::Descendants, $buttonCondition)) {
      $rect = $e.Current.BoundingRectangle
      # Recycled buttons are parked far above the screen.
      if ($rect.IsEmpty -or $rect.Y -lt -5000) { continue }
      $id = $e.Current.AutomationId
      if ($id -match '^Window: (0x[0-9a-f]+)') {
        $hwnd = [IntPtr][Convert]::ToInt64($Matches[1], 16)
        "  {0,-10} x={1,-6} w={2,-5} {3,-32} {4}" -f $Matches[1], $rect.X, $rect.Width, [UnglomProbe]::Aumid($hwnd), [UnglomProbe]::Title($hwnd)
      } elseif ($id -match '^Appid: (.*)') {
        "  {0,-10} x={1,-6} w={2,-5} {3,-32} {4}" -f 'pinned', $rect.X, $rect.Width, $Matches[1], $e.Current.Name
      }
    }
  }
}
