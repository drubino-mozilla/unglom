// Unglom.exe: tray app that loads UnglomTap.dll into the taskbar and keeps it
// loaded across Explorer restarts. The DLL undoes its changes when this
// process exits or the user pauses it.
//
// Command line:
//   Unglom.exe [--mode run|dump] [--dll <path>]   start (replacing any running copy)
//   Unglom.exe --stop                              stop the running copy
//   Unglom.exe --stop-this-copy                    stop it only if it is this same exe file

#include <windows.h>
#include <objbase.h>
#include <shellapi.h>
#include <shobjidl.h>

#include <algorithm>
#include <string>
#include <utility>
#include <vector>

#include "../common/Log.h"
#include "../common/PinSettings.h"

using namespace unglom;

namespace {

// {5a5848b4-a64b-4360-978b-a402c2902714}
constexpr CLSID CLSID_UnglomTap = {
    0x5a5848b4, 0xa64b, 0x4360, {0x97, 0x8b, 0xa4, 0x02, 0xc2, 0x90, 0x27, 0x14}};

constexpr int kIconId = 1;  // Unglom.rc
constexpr wchar_t kWindowClass[] = L"UnglomLoaderWindow";
constexpr wchar_t kRunKey[] = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
constexpr wchar_t kRunValue[] = L"Unglom";
constexpr UINT WM_TRAY = WM_APP + 1;
constexpr UINT WM_TASKBAR_SETTINGS_CHANGED = WM_APP + 2;
constexpr UINT_PTR kInjectTimer = 1;
constexpr ULONGLONG kCrashWindowMs = 30'000;
constexpr int kMaxQuickRestarts = 2;
constexpr int kMaxInjectAttempts = 15;

enum MenuId : UINT { kMenuToggle = 1, kMenuStartup, kMenuOpenLogs, kMenuExit, kMenuResetPins };
constexpr UINT kMenuPinBase = 1000;  // One item per (pin, monitor) pair from here.

using InitializeXamlDiagnosticsExFn = HRESULT(WINAPI*)(LPCWSTR, DWORD, LPCWSTR, LPCWSTR, CLSID,
                                                       LPCWSTR);

struct {
  std::wstring sourceDll;
  std::wstring mode = L"run";
  bool enabled = true;
  bool injected = false;
  HANDLE stopEvent = nullptr;
  ULONGLONG injectedAt = 0;
  int quickRestarts = 0;
  int injectAttempts = 0;
  UINT taskbarCreatedMsg = 0;
  NOTIFYICONDATAW icon = {};
  std::vector<std::pair<std::wstring, std::wstring>> pinMenu;  // (app id, monitor id)
} g;

std::wstring ExePath() {
  wchar_t path[MAX_PATH * 2];
  DWORD n = GetModuleFileNameW(nullptr, path, ARRAYSIZE(path));
  return std::wstring(path, n);
}

std::wstring ExeDir() {
  std::wstring path = ExePath();
  return path.substr(0, path.find_last_of(L'\\'));
}

// Explorer keeps every copy it has loaded, so each injection uses a fresh
// file name. Old copies that are no longer loaded get cleaned up here.
std::wstring StageDll() {
  std::wstring dir = DataDir() + L"\\bin";
  CreateDirectoryW(dir.c_str(), nullptr);
  WIN32_FIND_DATAW found;
  HANDLE find = FindFirstFileW((dir + L"\\UnglomTap-*.dll").c_str(), &found);
  if (find != INVALID_HANDLE_VALUE) {
    do {
      DeleteFileW((dir + L"\\" + found.cFileName).c_str());
    } while (FindNextFileW(find, &found));
    FindClose(find);
  }
  std::wstring staged = dir + L"\\UnglomTap-" + std::to_wstring(GetTickCount64()) + L".dll";
  if (!CopyFileW(g.sourceDll.c_str(), staged.c_str(), FALSE)) {
    Log(L"Copying %ls failed: %lu", g.sourceDll.c_str(), GetLastError());
    return {};
  }
  return staged;
}

void StopTap() {
  if (g.stopEvent) {
    SetEvent(g.stopEvent);
    CloseHandle(g.stopEvent);
    g.stopEvent = nullptr;
  }
  g.injected = false;
}

bool Inject() {
  StopTap();
  HWND tray = FindWindowW(L"Shell_TrayWnd", nullptr);
  if (!tray) {
    Log(L"Taskbar window not found");
    return false;
  }
  DWORD explorerPid = 0;
  GetWindowThreadProcessId(tray, &explorerPid);

  std::wstring dll = StageDll();
  if (dll.empty()) return false;

  std::wstring stopName = L"Local\\Unglom.Stop." + std::to_wstring(GetCurrentProcessId()) + L"." +
                          std::to_wstring(GetTickCount64());
  g.stopEvent = CreateEventW(nullptr, TRUE, FALSE, stopName.c_str());
  std::wstring init = L"pid=" + std::to_wstring(GetCurrentProcessId()) + L";stop=" + stopName +
                      L";mode=" + g.mode;

  HMODULE xaml = LoadLibraryExW(L"Windows.UI.Xaml.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
  auto initialize = xaml ? reinterpret_cast<InitializeXamlDiagnosticsExFn>(
                               GetProcAddress(xaml, "InitializeXamlDiagnosticsEx"))
                         : nullptr;
  if (!initialize) {
    Log(L"InitializeXamlDiagnosticsEx not available");
    return false;
  }

  // A connection name that is already in use fails with ERROR_NOT_FOUND.
  HRESULT hr = E_FAIL;
  for (int i = 1; i <= 100; ++i) {
    std::wstring connection = L"VisualDiagConnection" + std::to_wstring(i);
    hr = initialize(connection.c_str(), explorerPid, L"", dll.c_str(), CLSID_UnglomTap, init.c_str());
    if (hr != HRESULT_FROM_WIN32(ERROR_NOT_FOUND)) break;
  }
  Log(L"Injected %ls into explorer %lu: 0x%08X", dll.c_str(), explorerPid,
      static_cast<unsigned>(hr));
  if (FAILED(hr)) {
    StopTap();
    return false;
  }
  g.injected = true;
  g.injectedAt = GetTickCount64();
  return true;
}

void UpdateTrayIcon(DWORD message) {
  g.icon.uFlags = NIF_ICON | NIF_TIP | NIF_MESSAGE;
  g.icon.uCallbackMessage = WM_TRAY;
  if (!g.icon.hIcon) {
    int size = GetSystemMetricsForDpi(SM_CXSMICON, GetDpiForWindow(g.icon.hWnd));
    g.icon.hIcon = static_cast<HICON>(LoadImageW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(kIconId),
                                                 IMAGE_ICON, size, size, 0));
  }
  lstrcpynW(g.icon.szTip, g.enabled ? L"Unglom is on" : L"Unglom is paused",
            ARRAYSIZE(g.icon.szTip));
  Shell_NotifyIconW(message, &g.icon);
}

void Notify(const wchar_t* text) {
  g.icon.uFlags = NIF_INFO;
  lstrcpynW(g.icon.szInfoTitle, L"Unglom", ARRAYSIZE(g.icon.szInfoTitle));
  lstrcpynW(g.icon.szInfo, text, ARRAYSIZE(g.icon.szInfo));
  g.icon.dwInfoFlags = NIIF_INFO;
  Shell_NotifyIconW(NIM_MODIFY, &g.icon);
}

bool StartsWithWindows() {
  wchar_t value[MAX_PATH * 2];
  DWORD size = sizeof(value);
  return RegGetValueW(HKEY_CURRENT_USER, kRunKey, kRunValue, RRF_RT_REG_SZ, nullptr, value, &size) ==
         ERROR_SUCCESS;
}

void SetStartsWithWindows(bool on) {
  HKEY key;
  if (RegOpenKeyExW(HKEY_CURRENT_USER, kRunKey, 0, KEY_SET_VALUE, &key) != ERROR_SUCCESS) return;
  if (on) {
    std::wstring command = L"\"" + ExePath() + L"\"";
    RegSetValueExW(key, kRunValue, 0, REG_SZ, reinterpret_cast<const BYTE*>(command.c_str()),
                   static_cast<DWORD>((command.size() + 1) * sizeof(wchar_t)));
  } else {
    RegDeleteValueW(key, kRunValue);
  }
  RegCloseKey(key);
}

void ReleaseTaskbars() {
  TaskbarApps own;
  if (!ReadTakenOver(&own)) return;
  Log(L"Putting back \"Show my taskbar apps on\" = %lu", static_cast<DWORD>(own));
  WriteTaskbarAppsSetting(own);
  ClearTakenOver();
}

// Pins only reach other monitors' taskbars when Windows shows taskbar apps on
// all taskbars, so while any pin is assigned to monitors Unglom switches that on
// and the tap hides whatever the user's own setting wouldn't show.
void UpdateTaskbars() {
  bool want = g.enabled && g.injected && g.mode == L"run" && !LoadPinSettings().pinMonitors.empty();
  if (!want) {
    ReleaseTaskbars();
    return;
  }
  TaskbarApps own;
  if (!ReadTakenOver(&own)) {
    own = ReadTaskbarAppsSetting();
    Log(L"Showing taskbar apps on all taskbars (was %lu)", static_cast<DWORD>(own));
    WriteTakenOver(own);
  }
  if (ReadTaskbarAppsSetting() != TaskbarApps::AllTaskbars) {
    WriteTaskbarAppsSetting(TaskbarApps::AllTaskbars);
  }
}

// Someone changed "Show my taskbar apps on" while Unglom had it switched to all
// taskbars: treat the new value as the user's own choice.
void OnTaskbarSettingsChanged() {
  TaskbarApps own;
  if (!ReadTakenOver(&own)) return;
  TaskbarApps current = ReadTaskbarAppsSetting();
  if (current == TaskbarApps::AllTaskbars) return;
  Log(L"\"Show my taskbar apps on\" changed to %lu", static_cast<DWORD>(current));
  WriteTakenOver(current);
  WriteTaskbarAppsSetting(TaskbarApps::AllTaskbars);
}

TaskbarApps OwnTaskbarApps() {
  TaskbarApps own;
  return ReadTakenOver(&own) ? own : ReadTaskbarAppsSetting();
}

// The connected monitors a pin shows on, as the tap decides it.
std::vector<std::wstring> PinShownOn(const std::wstring& appId, const PinSettings& settings,
                                     const std::vector<Monitor>& monitors) {
  std::vector<std::wstring> shown;
  auto it = settings.pinMonitors.find(appId);
  if (it != settings.pinMonitors.end()) {
    for (const Monitor& m : monitors) {
      if (std::find(it->second.begin(), it->second.end(), m.id) != it->second.end()) {
        shown.push_back(m.id);
      }
    }
  }
  if (shown.empty()) {
    bool all = OwnTaskbarApps() == TaskbarApps::AllTaskbars;
    for (const Monitor& m : monitors) {
      if (all || m.primary) shown.push_back(m.id);
    }
  }
  return shown;
}

std::wstring AppsFolderName(const std::wstring& appId) {
  std::wstring name;
  IShellItem* item = nullptr;
  if (SUCCEEDED(SHCreateItemFromParsingName((L"shell:AppsFolder\\" + appId).c_str(), nullptr,
                                            IID_PPV_ARGS(&item)))) {
    LPWSTR display = nullptr;
    if (SUCCEEDED(item->GetDisplayName(SIGDN_NORMALDISPLAY, &display))) {
      name = display;
      CoTaskMemFree(display);
    }
    item->Release();
  }
  return name;
}

std::vector<PinnedApp> PinnedApps(const PinSettings& settings) {
  std::vector<PinnedApp> pins = ShortcutPins();
  auto add = [&](const std::wstring& appId, bool stillPinned) {
    for (const PinnedApp& pin : pins) {
      if (pin.appId == appId) return;
    }
    std::wstring name = AppsFolderName(appId);
    if (name.empty()) name = appId;
    if (!stillPinned) name += L" (no longer pinned)";
    pins.push_back({appId, name});
  };
  for (const std::wstring& appId : StorePins()) add(appId, true);
  for (const auto& [appId, monitorIds] : settings.pinMonitors) add(appId, false);
  std::sort(pins.begin(), pins.end(), [](const PinnedApp& a, const PinnedApp& b) {
    return CompareStringEx(LOCALE_NAME_USER_DEFAULT, NORM_IGNORECASE, a.name.c_str(), -1,
                           b.name.c_str(), -1, nullptr, nullptr, 0) == CSTR_LESS_THAN;
  });
  return pins;
}

std::wstring MenuText(std::wstring text) {
  for (size_t i = 0; (i = text.find(L'&', i)) != std::wstring::npos; i += 2) text.insert(i, 1, L'&');
  return text;
}

HMENU PinsMenu() {
  HMENU menu = CreatePopupMenu();
  g.pinMenu.clear();
  std::vector<Monitor> monitors = ConnectedMonitors();
  if (monitors.size() < 2) {
    AppendMenuW(menu, MF_STRING | MF_GRAYED, 0, L"Only one monitor is connected");
    return menu;
  }
  PinSettings settings = LoadPinSettings();
  for (const PinnedApp& pin : PinnedApps(settings)) {
    std::vector<std::wstring> shown = PinShownOn(pin.appId, settings, monitors);
    HMENU submenu = CreatePopupMenu();
    for (const Monitor& m : monitors) {
      bool checked = std::find(shown.begin(), shown.end(), m.id) != shown.end();
      AppendMenuW(submenu, MF_STRING | (checked ? MF_CHECKED : 0),
                  kMenuPinBase + static_cast<UINT>(g.pinMenu.size()), m.label.c_str());
      g.pinMenu.emplace_back(pin.appId, m.id);
    }
    AppendMenuW(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(submenu), MenuText(pin.name).c_str());
  }
  if (!settings.pinMonitors.empty()) {
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, kMenuResetPins, L"Put every pin back where Windows puts it");
  }
  return menu;
}

void TogglePinMonitor(size_t index) {
  if (index >= g.pinMenu.size()) return;
  auto [appId, monitorId] = g.pinMenu[index];
  PinSettings settings = LoadPinSettings();
  std::vector<Monitor> monitors = ConnectedMonitors();
  std::vector<std::wstring> shown = PinShownOn(appId, settings, monitors);
  auto it = std::find(shown.begin(), shown.end(), monitorId);
  if (it == shown.end()) {
    shown.push_back(monitorId);
  } else if (shown.size() > 1) {
    shown.erase(it);
  } else {
    return;  // A pin has to show somewhere.
  }

  settings.pinMonitors.erase(appId);
  std::vector<std::wstring> defaults = PinShownOn(appId, settings, monitors);
  std::sort(shown.begin(), shown.end());
  std::sort(defaults.begin(), defaults.end());
  Log(L"Pin %ls now shows on %zu monitor(s)", appId.c_str(), shown.size());
  SavePinMonitors(appId, shown == defaults ? std::vector<std::wstring>() : shown);
  UpdateTaskbars();
}

void ScheduleInject(UINT delayMs) {
  g.injectAttempts = 0;
  SetTimer(g.icon.hWnd, kInjectTimer, delayMs, nullptr);
}

void ShowMenu(HWND hwnd) {
  HMENU menu = CreatePopupMenu();
  AppendMenuW(menu, MF_STRING, kMenuToggle, g.enabled ? L"Pause Unglom" : L"Resume Unglom");
  AppendMenuW(menu, MF_STRING | (StartsWithWindows() ? MF_CHECKED : 0), kMenuStartup,
              L"Start with Windows");
  AppendMenuW(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(PinsMenu()), L"Pinned apps on each monitor");
  AppendMenuW(menu, MF_STRING, kMenuOpenLogs, L"Open log folder");
  AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
  AppendMenuW(menu, MF_STRING, kMenuExit, L"Exit");
  POINT pt;
  GetCursorPos(&pt);
  SetForegroundWindow(hwnd);
  TrackPopupMenu(menu, TPM_RIGHTBUTTON, pt.x, pt.y, 0, hwnd, nullptr);
  DestroyMenu(menu);
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam) {
  if (message == g.taskbarCreatedMsg && g.taskbarCreatedMsg) {
    Log(L"Taskbar restarted");
    UpdateTrayIcon(NIM_ADD);
    bool quick = g.injected && GetTickCount64() - g.injectedAt < kCrashWindowMs;
    g.quickRestarts = quick ? g.quickRestarts + 1 : 0;
    g.injected = false;
    if (g.quickRestarts >= kMaxQuickRestarts) {
      Log(L"Taskbar restarted %d times right after loading; pausing", g.quickRestarts);
      StopTap();
      g.enabled = false;
      UpdateTaskbars();
      UpdateTrayIcon(NIM_MODIFY);
      Notify(L"Paused because the taskbar restarted right after Unglom loaded. "
             L"Right-click the tray icon to resume.");
    } else if (g.enabled) {
      ScheduleInject(3000);
    }
    return 0;
  }

  switch (message) {
    case WM_TIMER:
      if (wparam == kInjectTimer) {
        KillTimer(hwnd, kInjectTimer);
        if (g.enabled && !Inject() && ++g.injectAttempts < kMaxInjectAttempts) {
          SetTimer(hwnd, kInjectTimer, 2000, nullptr);
        }
        UpdateTaskbars();
      }
      return 0;
    case WM_SETTINGCHANGE:
      if (lparam && lstrcmpiW(reinterpret_cast<LPCWSTR>(lparam), L"TraySettings") == 0) {
        // Not while the sender waits on this broadcast; the reaction broadcasts too.
        PostMessageW(hwnd, WM_TASKBAR_SETTINGS_CHANGED, 0, 0);
      }
      return 0;
    case WM_TASKBAR_SETTINGS_CHANGED:
      OnTaskbarSettingsChanged();
      return 0;
    case WM_TRAY:
      if (LOWORD(lparam) == WM_RBUTTONUP || LOWORD(lparam) == WM_LBUTTONUP) ShowMenu(hwnd);
      return 0;
    case WM_COMMAND:
      switch (LOWORD(wparam)) {
        case kMenuToggle:
          g.enabled = !g.enabled;
          g.quickRestarts = 0;
          if (g.enabled) {
            ScheduleInject(0);
          } else {
            StopTap();
            UpdateTaskbars();
          }
          UpdateTrayIcon(NIM_MODIFY);
          break;
        case kMenuResetPins:
          ClearPinMonitors();
          UpdateTaskbars();
          break;
        case kMenuStartup:
          SetStartsWithWindows(!StartsWithWindows());
          break;
        case kMenuOpenLogs:
          ShellExecuteW(nullptr, L"open", DataDir().c_str(), nullptr, nullptr, SW_SHOWNORMAL);
          break;
        case kMenuExit:
          DestroyWindow(hwnd);
          break;
        default:
          if (LOWORD(wparam) >= kMenuPinBase) TogglePinMonitor(LOWORD(wparam) - kMenuPinBase);
          break;
      }
      return 0;
    case WM_QUERYENDSESSION:
      return TRUE;
    case WM_ENDSESSION:
      if (wparam) {
        StopTap();
        ReleaseTaskbars();
      }
      return 0;
    case WM_DESTROY:
      StopTap();
      ReleaseTaskbars();
      Shell_NotifyIconW(NIM_DELETE, &g.icon);
      PostQuitMessage(0);
      return 0;
  }
  return DefWindowProcW(hwnd, message, wparam, lparam);
}

// Closes a running copy (if any) and waits for it to exit. With
// onlyThisCopy, a copy running from a different exe file is left alone.
void StopRunningInstance(bool onlyThisCopy) {
  HWND other = FindWindowW(kWindowClass, nullptr);
  if (!other) return;
  DWORD pid = 0;
  GetWindowThreadProcessId(other, &pid);
  HANDLE process = OpenProcess(SYNCHRONIZE | PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
  if (!process) return;
  if (onlyThisCopy) {
    wchar_t path[MAX_PATH * 2];
    DWORD size = ARRAYSIZE(path);
    bool same = QueryFullProcessImageNameW(process, 0, path, &size) &&
                CompareStringOrdinal(path, -1, ExePath().c_str(), -1, TRUE) == CSTR_EQUAL;
    if (!same) {
      CloseHandle(process);
      return;
    }
  }
  PostMessageW(other, WM_CLOSE, 0, 0);
  WaitForSingleObject(process, 5000);
  CloseHandle(process);
}

}  // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int) {
  LogInit(L"loader.log", L"loader");

  bool stopOnly = false;
  bool onlyThisCopy = false;
  g.sourceDll = ExeDir() + L"\\UnglomTap.dll";
  int argc = 0;
  LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
  for (int i = 1; i < argc; ++i) {
    std::wstring arg = argv[i];
    if (arg == L"--stop") stopOnly = true;
    if (arg == L"--stop-this-copy") stopOnly = onlyThisCopy = true;
    if (arg == L"--mode" && i + 1 < argc) g.mode = argv[++i];
    if (arg == L"--dll" && i + 1 < argc) g.sourceDll = argv[++i];
  }
  LocalFree(argv);

  StopRunningInstance(onlyThisCopy);
  if (stopOnly) {
    // Covers a copy that crashed while it had the taskbar setting switched.
    if (!onlyThisCopy) ReleaseTaskbars();
    return 0;
  }
  CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
  Log(L"Starting (mode=%ls, dll=%ls)", g.mode.c_str(), g.sourceDll.c_str());

  WNDCLASSW wc = {};
  wc.lpfnWndProc = WndProc;
  wc.hInstance = instance;
  wc.hIcon = LoadIconW(instance, MAKEINTRESOURCEW(kIconId));
  wc.lpszClassName = kWindowClass;
  RegisterClassW(&wc);
  HWND hwnd = CreateWindowW(kWindowClass, L"Unglom", 0, 0, 0, 0, 0, nullptr, nullptr, instance,
                            nullptr);
  if (!hwnd) return 1;

  g.taskbarCreatedMsg = RegisterWindowMessageW(L"TaskbarCreated");
  g.icon.cbSize = sizeof(g.icon);
  g.icon.hWnd = hwnd;
  g.icon.uID = 1;
  UpdateTrayIcon(NIM_ADD);
  ScheduleInject(0);

  MSG msg;
  while (GetMessageW(&msg, nullptr, 0, 0)) {
    TranslateMessage(&msg);
    DispatchMessageW(&msg);
  }
  return 0;
}
