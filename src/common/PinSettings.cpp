#include "PinSettings.h"

#include <algorithm>
#include <regex>

#include "AppResolver.h"

namespace unglom {

const wchar_t kTaskbandKey[] = L"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\Taskband";

namespace {

constexpr wchar_t kUnglomKey[] = L"Software\\Unglom";
constexpr wchar_t kPinMonitorsKey[] = L"Software\\Unglom\\PinMonitors";
constexpr wchar_t kIconAlignmentKey[] = L"Software\\Unglom\\IconAlignment";
constexpr wchar_t kWindowsAlignmentValue[] = L"TaskbarAl";
constexpr wchar_t kWindowTaskbarsValue[] = L"WindowTaskbars";
constexpr wchar_t kAdvancedKey[] = L"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\Advanced";
constexpr wchar_t kTaskbarAppsValue[] = L"MMTaskbarMode";

bool ReadDword(const wchar_t* key, const wchar_t* name, DWORD* value) {
  DWORD size = sizeof(*value);
  return RegGetValueW(HKEY_CURRENT_USER, key, name, RRF_RT_REG_DWORD, nullptr, value, &size) ==
         ERROR_SUCCESS;
}

void WriteDword(const wchar_t* key, const wchar_t* name, DWORD value) {
  RegSetKeyValueW(HKEY_CURRENT_USER, key, name, REG_DWORD, &value, sizeof(value));
}

// Value names of a key, lowercased.
std::vector<std::wstring> ValueNames(const wchar_t* keyPath) {
  std::vector<std::wstring> names;
  HKEY key;
  if (RegOpenKeyExW(HKEY_CURRENT_USER, keyPath, 0, KEY_READ, &key) != ERROR_SUCCESS) return names;
  wchar_t name[16384];
  for (DWORD i = 0;; ++i) {
    DWORD length = ARRAYSIZE(name);
    if (RegEnumValueW(key, i, name, &length, nullptr, nullptr, nullptr, nullptr) != ERROR_SUCCESS) {
      break;
    }
    names.push_back(Lowercase(std::wstring(name, length)));
  }
  RegCloseKey(key);
  return names;
}

std::vector<std::wstring> ReadMultiString(const wchar_t* key, const std::wstring& name) {
  DWORD size = 0;
  if (RegGetValueW(HKEY_CURRENT_USER, key, name.c_str(), RRF_RT_REG_MULTI_SZ, nullptr, nullptr,
                   &size) != ERROR_SUCCESS) {
    return {};
  }
  std::wstring buffer(size / sizeof(wchar_t) + 1, L'\0');
  if (RegGetValueW(HKEY_CURRENT_USER, key, name.c_str(), RRF_RT_REG_MULTI_SZ, nullptr,
                   buffer.data(), &size) != ERROR_SUCCESS) {
    return {};
  }
  std::vector<std::wstring> result;
  for (const wchar_t* p = buffer.c_str(); *p; p += wcslen(p) + 1) result.push_back(p);
  return result;
}

std::wstring MonitorId(const wchar_t* device) {
  DISPLAY_DEVICEW display = {sizeof(display)};
  for (DWORD i = 0; EnumDisplayDevicesW(device, i, &display, EDD_GET_DEVICE_INTERFACE_NAME); ++i) {
    if (display.StateFlags & DISPLAY_DEVICE_ACTIVE) return Lowercase(display.DeviceID);
    display.cb = sizeof(display);
  }
  return Lowercase(device);
}

}  // namespace

PinSettings LoadPinSettings() {
  PinSettings settings;
  settings.takenOver = ReadTakenOver(&settings.windows);
  for (const std::wstring& appId : ValueNames(kPinMonitorsKey)) {
    std::vector<std::wstring> ids = ReadMultiString(kPinMonitorsKey, appId);
    for (std::wstring& id : ids) id = Lowercase(id);
    if (!ids.empty()) settings.pinMonitors[appId] = ids;
  }
  for (const std::wstring& monitorId : ValueNames(kIconAlignmentKey)) {
    DWORD value = 0;
    if (ReadDword(kIconAlignmentKey, monitorId.c_str(), &value) && value >= 1 && value <= 3) {
      settings.iconAlignment[monitorId] = static_cast<IconAlignment>(value);
    }
  }
  return settings;
}

IconAlignment WindowsIconAlignment() {
  DWORD value = 1;  // Centered when the value is missing.
  ReadDword(kAdvancedKey, kWindowsAlignmentValue, &value);
  return value == 0 ? IconAlignment::Left : IconAlignment::Center;
}

void SaveIconAlignment(const std::wstring& monitorId, IconAlignment alignment) {
  if (alignment == WindowsIconAlignment()) {
    RegDeleteKeyValueW(HKEY_CURRENT_USER, kIconAlignmentKey, monitorId.c_str());
  } else {
    WriteDword(kIconAlignmentKey, monitorId.c_str(), static_cast<DWORD>(alignment));
  }
}

void SavePinMonitors(const std::wstring& appId, const std::vector<std::wstring>& monitorIds) {
  if (monitorIds.empty()) {
    RegDeleteKeyValueW(HKEY_CURRENT_USER, kPinMonitorsKey, appId.c_str());
    return;
  }
  std::wstring data;
  for (const std::wstring& id : monitorIds) data += id + L'\0';
  data += L'\0';
  RegSetKeyValueW(HKEY_CURRENT_USER, kPinMonitorsKey, appId.c_str(), REG_MULTI_SZ, data.c_str(),
                  static_cast<DWORD>(data.size() * sizeof(wchar_t)));
}

void ClearPinMonitors() {
  for (const std::wstring& appId : ValueNames(kPinMonitorsKey)) SavePinMonitors(appId, {});
}

TaskbarApps ReadTaskbarAppsSetting() {
  DWORD value = 0;  // Windows' default when the value is missing.
  ReadDword(kAdvancedKey, kTaskbarAppsValue, &value);
  return value <= 2 ? static_cast<TaskbarApps>(value) : TaskbarApps::AllTaskbars;
}

void WriteTaskbarAppsSetting(TaskbarApps value) {
  WriteDword(kAdvancedKey, kTaskbarAppsValue, static_cast<DWORD>(value));
  DWORD_PTR result;
  SendMessageTimeoutW(HWND_BROADCAST, WM_SETTINGCHANGE, 0,
                      reinterpret_cast<LPARAM>(L"TraySettings"), SMTO_ABORTIFHUNG, 1000, &result);
}

bool ReadTakenOver(TaskbarApps* windows) {
  DWORD value;
  if (!ReadDword(kUnglomKey, kWindowTaskbarsValue, &value) || value > 2) return false;
  *windows = static_cast<TaskbarApps>(value);
  return true;
}

void WriteTakenOver(TaskbarApps windows) {
  WriteDword(kUnglomKey, kWindowTaskbarsValue, static_cast<DWORD>(windows));
}

void ClearTakenOver() { RegDeleteKeyValueW(HKEY_CURRENT_USER, kUnglomKey, kWindowTaskbarsValue); }

std::vector<Monitor> ConnectedMonitors() {
  struct Found {
    Monitor monitor;
    RECT rect;
  };
  std::vector<Found> found;
  EnumDisplayMonitors(
      nullptr, nullptr,
      [](HMONITOR handle, HDC, LPRECT, LPARAM param) -> BOOL {
        MONITORINFOEXW info = {};
        info.cbSize = sizeof(info);
        if (!GetMonitorInfoW(handle, &info)) return TRUE;
        Found f;
        f.monitor.handle = handle;
        f.monitor.id = MonitorId(info.szDevice);
        f.monitor.primary = (info.dwFlags & MONITORINFOF_PRIMARY) != 0;
        f.rect = info.rcMonitor;
        reinterpret_cast<std::vector<Found>*>(param)->push_back(f);
        return TRUE;
      },
      reinterpret_cast<LPARAM>(&found));

  bool stacked = std::all_of(found.begin(), found.end(),
                             [&](const Found& f) { return f.rect.left == found[0].rect.left; });
  std::sort(found.begin(), found.end(), [&](const Found& a, const Found& b) {
    return stacked ? a.rect.top < b.rect.top : a.rect.left < b.rect.left;
  });

  std::vector<Monitor> monitors;
  for (size_t i = 0; i < found.size(); ++i) {
    std::wstring label;
    if (found.size() == 1) {
      label = L"This monitor";
    } else if (found.size() <= 3) {
      static const wchar_t* sideBySide[] = {L"Left", L"Middle", L"Right"};
      static const wchar_t* stackedNames[] = {L"Top", L"Middle", L"Bottom"};
      size_t slot = (found.size() == 2 && i == 1) ? 2 : i;
      label = std::wstring(stacked ? stackedNames[slot] : sideBySide[slot]) + L" monitor";
    } else {
      label = L"Monitor " + std::to_wstring(i + 1);
    }
    if (found[i].monitor.primary) label += L" (main)";
    found[i].monitor.label = label;
    monitors.push_back(found[i].monitor);
  }
  return monitors;
}

std::wstring PinnedShortcutsDir() {
  wchar_t appData[MAX_PATH] = {};
  GetEnvironmentVariableW(L"APPDATA", appData, MAX_PATH);
  return std::wstring(appData) + L"\\Microsoft\\Internet Explorer\\Quick Launch\\User Pinned\\TaskBar";
}

std::vector<PinnedApp> ShortcutPins() {
  std::vector<PinnedApp> pins;
  std::wstring dir = PinnedShortcutsDir();
  WIN32_FIND_DATAW found;
  HANDLE find = FindFirstFileW((dir + L"\\*.lnk").c_str(), &found);
  if (find == INVALID_HANDLE_VALUE) return pins;
  do {
    std::wstring name = found.cFileName;
    std::wstring path = dir + L"\\" + name;
    std::wstring appId = TaskbarAppIdForShortcut(path);
    if (!appId.empty()) pins.push_back({appId, name.substr(0, name.size() - 4), path});
  } while (FindNextFileW(find, &found));
  FindClose(find);
  return pins;
}

// The pin list is a blob of item ID lists; Store app entries carry their app ID
// ("<package family name>!<app>") as UTF-16 text, at either byte alignment.
std::vector<std::wstring> StorePins() {
  DWORD size = 0;
  if (RegGetValueW(HKEY_CURRENT_USER, kTaskbandKey, L"Favorites", RRF_RT_REG_BINARY, nullptr,
                   nullptr, &size) != ERROR_SUCCESS) {
    return {};
  }
  std::vector<BYTE> blob(size);
  if (RegGetValueW(HKEY_CURRENT_USER, kTaskbandKey, L"Favorites", RRF_RT_REG_BINARY, nullptr,
                   blob.data(), &size) != ERROR_SUCCESS) {
    return {};
  }
  static const std::wregex kStoreAppId(L"[A-Za-z0-9.\\-]+_[a-z0-9]{13}![A-Za-z0-9.\\-_]+");
  std::vector<std::wstring> pins;
  for (size_t alignment = 0; alignment < 2; ++alignment) {
    std::wstring text((size - alignment) / 2, L'\0');
    memcpy(text.data(), blob.data() + alignment, text.size() * sizeof(wchar_t));
    for (std::wsregex_iterator it(text.begin(), text.end(), kStoreAppId), end; it != end; ++it) {
      std::wstring appId = Lowercase(it->str());
      if (std::find(pins.begin(), pins.end(), appId) == pins.end()) pins.push_back(appId);
    }
  }
  return pins;
}

}  // namespace unglom
