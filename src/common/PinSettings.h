#pragma once

#include <windows.h>

#include <map>
#include <string>
#include <vector>

namespace unglom {

// Values of Windows' "Show my taskbar apps on" setting (MMTaskbarMode).
enum class TaskbarApps : DWORD { AllTaskbars = 0, MainAndWhereOpen = 1, WhereOpen = 2 };

// Per-monitor pins, stored under HKCU\Software\Unglom:
//   WindowTaskbars          present only while Unglom has switched Windows to "All taskbars"
//                           and hides buttons itself; holds the user's own TaskbarApps choice
//   PinMonitors\<app id>    the MonitorIds a pin shows on (REG_MULTI_SZ). Pins without a
//                           value, or whose monitors are all disconnected, show where
//                           Windows would show them.
struct PinSettings {
  bool takenOver = false;
  TaskbarApps windows = TaskbarApps::WhereOpen;
  std::map<std::wstring, std::vector<std::wstring>> pinMonitors;  // Keys are lowercase app IDs.
};

PinSettings LoadPinSettings();
void SavePinMonitors(const std::wstring& appId, const std::vector<std::wstring>& monitorIds);
void ClearPinMonitors();

TaskbarApps ReadTaskbarAppsSetting();
void WriteTaskbarAppsSetting(TaskbarApps value);
bool ReadTakenOver(TaskbarApps* windows);
void WriteTakenOver(TaskbarApps windows);
void ClearTakenOver();

struct Monitor {
  HMONITOR handle = nullptr;
  std::wstring id;     // Stable across reboots, unlike \\.\DISPLAYn.
  std::wstring label;  // "Left monitor (main)"
  bool primary = false;
};
// Sorted left to right (top to bottom when stacked).
std::vector<Monitor> ConnectedMonitors();

struct PinnedApp {
  std::wstring appId;
  std::wstring name;
};
// Taskbar pins that have a shortcut, with the shortcut's name.
std::vector<PinnedApp> ShortcutPins();
// App IDs of pinned Store apps, which have no shortcut.
std::vector<std::wstring> StorePins();

// Where Windows keeps the taskbar's pins, for watching them change.
std::wstring PinnedShortcutsDir();
extern const wchar_t kTaskbandKey[];

}  // namespace unglom
