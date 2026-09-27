#include "PinsWindow.h"

#include <commctrl.h>
#include <dwmapi.h>
#include <shobjidl.h>
#include <uxtheme.h>
#include <vssym32.h>

#include <algorithm>
#include <string>
#include <vector>

#include "../common/Log.h"
#include "../common/PinSettings.h"

namespace unglom {
namespace {

constexpr wchar_t kWindowClass[] = L"UnglomPinsWindow";
constexpr wchar_t kHint[] =
    L"Choose how each monitor's taskbar aligns its icons, and tick the monitors whose taskbar "
    L"each pinned app should appear on. Changes apply right away. Every app needs at least one "
    L"monitor.";
constexpr wchar_t kAlignmentLabel[] = L"Icon alignment";
constexpr const wchar_t* kAlignmentNames[] = {L"Left", L"Center", L"Right"};  // IconAlignment - 1
constexpr wchar_t kResetText[] = L"Put every pin back where Windows puts it";
constexpr INT_PTR kListId = 100;
constexpr INT_PTR kResetId = 101;  // Close is IDCANCEL, so Esc closes too.
constexpr INT_PTR kAlignmentIdBase = 200;  // One dropdown per monitor from here.
constexpr UINT WM_APP_LAYOUT = WM_APP + 1;

// Sizes in DIPs.
constexpr int kMargin = 12;
constexpr int kGap = 8;
constexpr int kNameColumn = 220;
constexpr int kMonitorColumn = 110;
constexpr int kIconSize = 24;
constexpr int kButtonHeight = 28;

struct {
  HWND hwnd = nullptr;
  HWND hint = nullptr;
  HWND list = nullptr;
  HWND reset = nullptr;
  HWND close = nullptr;
  HWND alignmentLabel = nullptr;
  std::vector<HWND> alignments;  // Per monitor, above its column.
  HFONT font = nullptr;
  HTHEME checkTheme = nullptr;
  UINT dpi = 96;
  bool dark = false;
  COLORREF background = 0;
  COLORREF text = 0;
  HBRUSH backgroundBrush = nullptr;
  std::function<void()> onChanged;
  PinSettings settings;
  std::wstring pinsVersion;
  std::vector<PinnedApp> pins;
  std::vector<Monitor> monitors;
  std::vector<std::vector<bool>> shown;  // [pin][monitor]
} w;

int Scale(int dips) { return MulDiv(dips, static_cast<int>(w.dpi), 96); }

void Reload();
void Layout();

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

IShellItem* ShellItemForPin(const PinnedApp& pin) {
  std::wstring path = pin.shortcut.empty() ? L"shell:AppsFolder\\" + pin.appId : pin.shortcut;
  IShellItem* item = nullptr;
  SHCreateItemFromParsingName(path.c_str(), nullptr, IID_PPV_ARGS(&item));
  return item;
}

std::wstring AppsFolderName(const PinnedApp& pin) {
  std::wstring name;
  if (IShellItem* item = ShellItemForPin(pin)) {
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
    PinnedApp pin{appId};
    pin.name = AppsFolderName(pin);
    if (pin.name.empty()) pin.name = appId;
    if (!stillPinned) pin.name += L" (no longer pinned)";
    pins.push_back(pin);
  };
  for (const std::wstring& appId : StorePins()) add(appId, true);
  for (const auto& [appId, monitorIds] : settings.pinMonitors) add(appId, false);
  std::sort(pins.begin(), pins.end(), [](const PinnedApp& a, const PinnedApp& b) {
    return CompareStringEx(LOCALE_NAME_USER_DEFAULT, NORM_IGNORECASE, a.name.c_str(), -1,
                           b.name.c_str(), -1, nullptr, nullptr, 0) == CSTR_LESS_THAN;
  });
  return pins;
}

int IconFor(const PinnedApp& pin, HIMAGELIST images, int size) {
  int index = -1;
  IShellItem* item = ShellItemForPin(pin);
  IShellItemImageFactory* factory = nullptr;
  if (item && SUCCEEDED(item->QueryInterface(IID_PPV_ARGS(&factory)))) {
    // Some Store apps only offer their logo as a thumbnail.
    HBITMAP bitmap = nullptr;
    if (SUCCEEDED(factory->GetImage({size, size}, SIIGBF_ICONONLY, &bitmap)) ||
        SUCCEEDED(factory->GetImage({size, size}, SIIGBF_RESIZETOFIT, &bitmap))) {
      index = ImageList_Add(images, bitmap, nullptr);
      DeleteObject(bitmap);
    }
    factory->Release();
  }
  if (item) item->Release();
  return index;
}

void UpdateShown() {
  w.shown.assign(w.pins.size(), std::vector<bool>(w.monitors.size()));
  for (size_t p = 0; p < w.pins.size(); ++p) {
    std::vector<std::wstring> ids = PinShownOn(w.pins[p].appId, w.settings, w.monitors);
    for (size_t m = 0; m < w.monitors.size(); ++m) {
      w.shown[p][m] = std::find(ids.begin(), ids.end(), w.monitors[m].id) != ids.end();
    }
  }
  EnableWindow(w.reset, !w.settings.pinMonitors.empty());
  InvalidateRect(w.list, nullptr, FALSE);

  IconAlignment windows = WindowsIconAlignment();
  for (size_t m = 0; m < w.alignments.size() && m < w.monitors.size(); ++m) {
    auto it = w.settings.iconAlignment.find(w.monitors[m].id);
    IconAlignment alignment = it == w.settings.iconAlignment.end() ? windows : it->second;
    SendMessageW(w.alignments[m], CB_SETCURSEL, static_cast<DWORD>(alignment) - 1, 0);
  }
}

void CreateAlignmentChoices() {
  for (HWND combo : w.alignments) DestroyWindow(combo);
  w.alignments.clear();
  auto instance = reinterpret_cast<HINSTANCE>(GetWindowLongPtrW(w.hwnd, GWLP_HINSTANCE));
  HWND previous = w.alignmentLabel;
  for (size_t m = 0; m < w.monitors.size(); ++m) {
    HWND combo = CreateWindowW(WC_COMBOBOXW, L"",
                               WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_VSCROLL | CBS_DROPDOWNLIST, 0, 0,
                               Scale(100), Scale(200), w.hwnd,
                               reinterpret_cast<HMENU>(kAlignmentIdBase + static_cast<INT_PTR>(m)),
                               instance, nullptr);
    for (const wchar_t* name : kAlignmentNames) {
      SendMessageW(combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(name));
    }
    SendMessageW(combo, WM_SETFONT, reinterpret_cast<WPARAM>(w.font), FALSE);
    SetWindowTheme(combo, w.dark ? L"DarkMode_CFD" : nullptr, nullptr);
    // Tab order follows the monitor columns, before the list.
    SetWindowPos(combo, previous, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    previous = combo;
    w.alignments.push_back(combo);
  }
}

void OnAlignmentChosen(size_t monitor) {
  if (monitor >= w.monitors.size() || monitor >= w.alignments.size()) return;
  LRESULT selection = SendMessageW(w.alignments[monitor], CB_GETCURSEL, 0, 0);
  if (selection < 0 || selection > 2) return;
  auto alignment = static_cast<IconAlignment>(selection + 1);
  Log(L"%ls icons now aligned %ls", w.monitors[monitor].label.c_str(), kAlignmentNames[selection]);
  SaveIconAlignment(w.monitors[monitor].id, alignment);
  Reload();
}

void FillList() {
  SendMessageW(w.list, WM_SETREDRAW, FALSE, 0);
  ListView_DeleteAllItems(w.list);
  while (ListView_DeleteColumn(w.list, 0)) {
  }

  int iconSize = Scale(kIconSize);
  HIMAGELIST images = ImageList_Create(iconSize, iconSize, ILC_COLOR32, 0, 8);
  HIMAGELIST old = ListView_SetImageList(w.list, images, LVSIL_SMALL);
  if (old) ImageList_Destroy(old);

  LVCOLUMNW column = {LVCF_TEXT | LVCF_WIDTH | LVCF_FMT};
  column.fmt = LVCFMT_LEFT;
  column.cx = Scale(kNameColumn);
  column.pszText = const_cast<wchar_t*>(L"Pinned app");
  ListView_InsertColumn(w.list, 0, &column);
  column.fmt = LVCFMT_CENTER;
  for (size_t m = 0; m < w.monitors.size(); ++m) {
    column.pszText = const_cast<wchar_t*>(w.monitors[m].label.c_str());
    column.cx = Scale(kMonitorColumn);
    ListView_InsertColumn(w.list, static_cast<int>(m) + 1, &column);
    ListView_SetColumnWidth(w.list, static_cast<int>(m) + 1, LVSCW_AUTOSIZE_USEHEADER);
    if (ListView_GetColumnWidth(w.list, static_cast<int>(m) + 1) < Scale(kMonitorColumn)) {
      ListView_SetColumnWidth(w.list, static_cast<int>(m) + 1, Scale(kMonitorColumn));
    }
  }

  for (size_t p = 0; p < w.pins.size(); ++p) {
    LVITEMW item = {LVIF_TEXT | LVIF_IMAGE};
    item.iItem = static_cast<int>(p);
    item.pszText = const_cast<wchar_t*>(w.pins[p].name.c_str());
    item.iImage = IconFor(w.pins[p], images, iconSize);
    ListView_InsertItem(w.list, &item);
  }
  SendMessageW(w.list, WM_SETREDRAW, TRUE, 0);
  CreateAlignmentChoices();
}

// Changes whenever the list of pinned apps might have, and is far cheaper to
// get than the list itself.
std::wstring PinnedAppsVersion() {
  std::wstring version;
  WIN32_FIND_DATAW found;
  HANDLE find = FindFirstFileW((PinnedShortcutsDir() + L"\\*.lnk").c_str(), &found);
  if (find != INVALID_HANDLE_VALUE) {
    do {
      version += std::wstring(found.cFileName) + L"|" +
                 std::to_wstring(found.ftLastWriteTime.dwLowDateTime) + L";";
    } while (FindNextFileW(find, &found));
    FindClose(find);
  }
  for (const std::wstring& appId : StorePins()) version += appId + L";";
  for (const auto& [appId, monitorIds] : w.settings.pinMonitors) version += appId + L";";
  return version;
}

bool SameIds(const std::vector<PinnedApp>& a, const std::vector<PinnedApp>& b) {
  return std::equal(a.begin(), a.end(), b.begin(), b.end(),
                    [](const PinnedApp& x, const PinnedApp& y) { return x.appId == y.appId; });
}

bool SameIds(const std::vector<Monitor>& a, const std::vector<Monitor>& b) {
  return std::equal(a.begin(), a.end(), b.begin(), b.end(), [](const Monitor& x, const Monitor& y) {
    return x.id == y.id && x.label == y.label;
  });
}

// Re-reads pins, monitors and assignments; rebuilds the list only if the pins
// or monitors changed, so selection and scrolling survive.
void Reload() {
  w.settings = LoadPinSettings();
  std::wstring pinsVersion = PinnedAppsVersion();
  std::vector<Monitor> monitors = ConnectedMonitors();
  bool pinsChanged = pinsVersion != w.pinsVersion;
  if (pinsChanged) {
    w.pinsVersion = pinsVersion;
    std::vector<PinnedApp> pins = PinnedApps(w.settings);
    pinsChanged = !SameIds(pins, w.pins);
    w.pins = std::move(pins);
  }
  if (pinsChanged || !SameIds(monitors, w.monitors)) {
    w.monitors = std::move(monitors);
    FillList();
    Layout();
  }
  UpdateShown();
}

void Toggle(size_t pin, size_t monitor) {
  if (pin >= w.pins.size() || monitor >= w.monitors.size()) return;
  const std::wstring appId = w.pins[pin].appId;
  std::vector<std::wstring> shown = PinShownOn(appId, w.settings, w.monitors);
  auto it = std::find(shown.begin(), shown.end(), w.monitors[monitor].id);
  if (it == shown.end()) {
    shown.push_back(w.monitors[monitor].id);
  } else if (shown.size() > 1) {
    shown.erase(it);
  } else {
    MessageBeep(MB_OK);  // A pin has to show somewhere.
    return;
  }

  PinSettings without = w.settings;
  without.pinMonitors.erase(appId);
  std::vector<std::wstring> defaults = PinShownOn(appId, without, w.monitors);
  std::sort(shown.begin(), shown.end());
  std::sort(defaults.begin(), defaults.end());
  Log(L"Pin %ls now shows on %zu monitor(s)", appId.c_str(), shown.size());
  SavePinMonitors(appId, shown == defaults ? std::vector<std::wstring>() : shown);
  w.onChanged();
  Reload();
}

void DrawCheckbox(HDC dc, const RECT& cell, bool checked) {
  if (w.checkTheme) {
    int state = checked ? CBS_CHECKEDNORMAL : CBS_UNCHECKEDNORMAL;
    SIZE size = {};
    GetThemePartSize(w.checkTheme, dc, BP_CHECKBOX, state, nullptr, TS_DRAW, &size);
    RECT box = {(cell.left + cell.right - size.cx) / 2, (cell.top + cell.bottom - size.cy) / 2};
    box.right = box.left + size.cx;
    box.bottom = box.top + size.cy;
    DrawThemeBackground(w.checkTheme, dc, BP_CHECKBOX, state, &box, nullptr);
    return;
  }
  int size = Scale(13);
  RECT box = {(cell.left + cell.right - size) / 2, (cell.top + cell.bottom - size) / 2};
  box.right = box.left + size;
  box.bottom = box.top + size;
  DrawFrameControl(dc, &box, DFC_BUTTON, DFCS_BUTTONCHECK | (checked ? DFCS_CHECKED : 0));
}

LRESULT OnListCustomDraw(NMLVCUSTOMDRAW* draw) {
  switch (draw->nmcd.dwDrawStage) {
    case CDDS_PREPAINT:
      return CDRF_NOTIFYITEMDRAW;
    case CDDS_ITEMPREPAINT:
      return CDRF_NOTIFYPOSTPAINT;
    case CDDS_ITEMPOSTPAINT: {
      size_t row = draw->nmcd.dwItemSpec;
      if (row >= w.shown.size()) break;
      for (size_t m = 0; m < w.monitors.size(); ++m) {
        RECT cell;
        ListView_GetSubItemRect(w.list, static_cast<int>(row), static_cast<int>(m) + 1, LVIR_BOUNDS,
                                &cell);
        DrawCheckbox(draw->nmcd.hdc, cell, w.shown[row][m]);
      }
      break;
    }
  }
  return CDRF_DODEFAULT;
}

// The header reports to the list, not to this window. Its dark theme keeps
// light-theme text colors, so set them here.
LRESULT CALLBACK ListSubclassProc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam, UINT_PTR,
                                  DWORD_PTR) {
  // The alignment dropdowns follow the columns.
  if (message == WM_HSCROLL) PostMessageW(GetParent(hwnd), WM_APP_LAYOUT, 0, 0);
  if (message == WM_NOTIFY) {
    auto* header = reinterpret_cast<NMHDR*>(lparam);
    if (header->hwndFrom == ListView_GetHeader(hwnd) &&
        (header->code == HDN_ITEMCHANGEDW || header->code == HDN_ITEMCHANGEDA)) {
      PostMessageW(GetParent(hwnd), WM_APP_LAYOUT, 0, 0);
    }
    if (header->hwndFrom == ListView_GetHeader(hwnd) && header->code == NM_CUSTOMDRAW) {
      auto* draw = reinterpret_cast<NMCUSTOMDRAW*>(lparam);
      if (draw->dwDrawStage == CDDS_PREPAINT) return CDRF_NOTIFYITEMDRAW;
      if (draw->dwDrawStage == CDDS_ITEMPREPAINT) {
        SetTextColor(draw->hdc, w.text);
        return CDRF_DODEFAULT;
      }
    }
  }
  return DefSubclassProc(hwnd, message, wparam, lparam);
}

void OnListClick(POINT point) {
  LVHITTESTINFO hit = {};
  hit.pt = point;
  ListView_SubItemHitTest(w.list, &hit);
  if (hit.iItem >= 0 && hit.iSubItem >= 1) Toggle(hit.iItem, hit.iSubItem - 1);
}

int TextHeight(const wchar_t* text, int width) {
  HDC dc = GetDC(w.hwnd);
  HGDIOBJ old = SelectObject(dc, w.font);
  RECT rect = {0, 0, width, 0};
  DrawTextW(dc, text, -1, &rect, DT_CALCRECT | DT_WORDBREAK | DT_NOPREFIX);
  SelectObject(dc, old);
  ReleaseDC(w.hwnd, dc);
  return rect.bottom;
}

int TextWidth(const wchar_t* text) {
  HDC dc = GetDC(w.hwnd);
  HGDIOBJ old = SelectObject(dc, w.font);
  SIZE size = {};
  GetTextExtentPoint32W(dc, text, lstrlenW(text), &size);
  SelectObject(dc, old);
  ReleaseDC(w.hwnd, dc);
  return size.cx;
}

int AlignmentRowHeight() {
  if (w.alignments.empty()) return 0;
  RECT combo;
  GetWindowRect(w.alignments[0], &combo);  // A dropdown list's closed height.
  return combo.bottom - combo.top;
}

void Layout() {
  RECT client;
  GetClientRect(w.hwnd, &client);
  int margin = Scale(kMargin), gap = Scale(kGap), buttonHeight = Scale(kButtonHeight);
  int width = client.right - 2 * margin;
  int hintHeight = TextHeight(kHint, width);
  int buttonsTop = client.bottom - margin - buttonHeight;
  int rowTop = margin + hintHeight + gap;
  int rowHeight = AlignmentRowHeight();
  int listTop = rowTop + rowHeight + gap;
  MoveWindow(w.hint, margin, margin, width, hintHeight, TRUE);
  MoveWindow(w.list, margin, listTop, width, std::max(0, buttonsTop - gap - listTop), TRUE);

  // Each dropdown sits above its monitor's column.
  HWND header = ListView_GetHeader(w.list);
  RECT nameColumn = {};
  Header_GetItemRect(header, 0, &nameColumn);
  MapWindowPoints(header, w.hwnd, reinterpret_cast<POINT*>(&nameColumn), 2);
  MoveWindow(w.alignmentLabel, nameColumn.left + Scale(6), rowTop, nameColumn.right - nameColumn.left,
             rowHeight, TRUE);
  for (size_t m = 0; m < w.alignments.size(); ++m) {
    RECT column = {};
    Header_GetItemRect(header, static_cast<int>(m) + 1, &column);
    MapWindowPoints(header, w.hwnd, reinterpret_cast<POINT*>(&column), 2);
    int comboWidth = std::min<int>(column.right - column.left - Scale(8), Scale(110));
    MoveWindow(w.alignments[m], (column.left + column.right - comboWidth) / 2, rowTop, comboWidth,
               Scale(200), TRUE);
  }
  int resetWidth = TextWidth(kResetText) + Scale(32);
  int closeWidth = Scale(90);
  MoveWindow(w.reset, margin, buttonsTop, resetWidth, buttonHeight, TRUE);
  MoveWindow(w.close, client.right - margin - closeWidth, buttonsTop, closeWidth, buttonHeight, TRUE);
}

// Fits the window to the list, centered on the monitor it is on, within its work area.
void SizeToContent() {
  int listWidth = GetSystemMetricsForDpi(SM_CXVSCROLL, w.dpi) + Scale(4);
  for (int c = 0; c <= static_cast<int>(w.monitors.size()); ++c) {
    listWidth += ListView_GetColumnWidth(w.list, c);
  }
  int margin = Scale(kMargin), gap = Scale(kGap);
  int clientWidth = std::max(listWidth, TextWidth(kResetText) + Scale(32 + 90) + gap) + 2 * margin;

  RECT header = {};
  GetWindowRect(ListView_GetHeader(w.list), &header);
  RECT row = {};
  int rowHeight = ListView_GetItemRect(w.list, 0, &row, LVIR_BOUNDS) ? row.bottom - row.top : Scale(28);
  int listHeight = (header.bottom - header.top) + rowHeight * ListView_GetItemCount(w.list) + Scale(4);
  int clientHeight = margin + TextHeight(kHint, clientWidth - 2 * margin) + gap +
                     AlignmentRowHeight() + gap + listHeight + gap + Scale(kButtonHeight) + margin;

  RECT frame = {0, 0, clientWidth, clientHeight};
  AdjustWindowRectExForDpi(&frame, GetWindowLongW(w.hwnd, GWL_STYLE), FALSE,
                           GetWindowLongW(w.hwnd, GWL_EXSTYLE), w.dpi);
  MONITORINFO info = {sizeof(info)};
  GetMonitorInfoW(MonitorFromWindow(w.hwnd, MONITOR_DEFAULTTONEAREST), &info);
  const RECT& area = info.rcWork;
  int width = std::min<int>(frame.right - frame.left, area.right - area.left);
  int height = std::min<int>(frame.bottom - frame.top, (area.bottom - area.top) * 9 / 10);
  SetWindowPos(w.hwnd, nullptr, area.left + (area.right - area.left - width) / 2,
               area.top + (area.bottom - area.top - height) / 2, width, height,
               SWP_NOZORDER | SWP_NOACTIVATE);
}

bool AppsUseDarkTheme() {
  DWORD light = 1;
  DWORD size = sizeof(light);
  RegGetValueW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
               L"AppsUseLightTheme", RRF_RT_REG_DWORD, nullptr, &light, &size);
  return light == 0;
}

// Follows the Windows light/dark app setting; Store app icons are drawn for it.
// The DarkMode_* theme names are what Windows' own dark Win32 windows use.
void ApplyTheme() {
  w.dark = AppsUseDarkTheme();
  BOOL darkTitleBar = w.dark;
  DwmSetWindowAttribute(w.hwnd, DWMWA_USE_IMMERSIVE_DARK_MODE, &darkTitleBar, sizeof(darkTitleBar));
  w.background = w.dark ? RGB(32, 32, 32) : GetSysColor(COLOR_WINDOW);
  w.text = w.dark ? RGB(255, 255, 255) : GetSysColor(COLOR_WINDOWTEXT);
  if (w.backgroundBrush) DeleteObject(w.backgroundBrush);
  w.backgroundBrush = CreateSolidBrush(w.background);

  SetWindowTheme(w.list, w.dark ? L"DarkMode_Explorer" : L"Explorer", nullptr);
  SetWindowTheme(ListView_GetHeader(w.list), w.dark ? L"DarkMode_ItemsView" : nullptr, nullptr);
  for (HWND button : {w.reset, w.close}) SetWindowTheme(button, w.dark ? L"DarkMode_Explorer" : nullptr, nullptr);
  for (HWND combo : w.alignments) SetWindowTheme(combo, w.dark ? L"DarkMode_CFD" : nullptr, nullptr);
  ListView_SetBkColor(w.list, w.background);
  ListView_SetTextBkColor(w.list, w.background);
  ListView_SetTextColor(w.list, w.text);

  if (w.checkTheme) CloseThemeData(w.checkTheme);
  w.checkTheme = w.dark ? OpenThemeDataForDpi(w.list, L"DarkMode_Explorer::Button", w.dpi) : nullptr;
  if (!w.checkTheme) w.checkTheme = OpenThemeDataForDpi(w.list, L"Button", w.dpi);
  RedrawWindow(w.hwnd, nullptr, nullptr, RDW_ERASE | RDW_INVALIDATE | RDW_ALLCHILDREN | RDW_FRAME);
}

void ApplyDpi() {
  if (w.font) DeleteObject(w.font);
  NONCLIENTMETRICSW metrics = {sizeof(metrics)};
  SystemParametersInfoForDpi(SPI_GETNONCLIENTMETRICS, sizeof(metrics), &metrics, 0, w.dpi);
  w.font = CreateFontIndirectW(&metrics.lfMessageFont);
  for (HWND child : {w.hint, w.alignmentLabel, w.list, w.reset, w.close}) {
    SendMessageW(child, WM_SETFONT, reinterpret_cast<WPARAM>(w.font), TRUE);
  }
  for (HWND combo : w.alignments) SendMessageW(combo, WM_SETFONT, reinterpret_cast<WPARAM>(w.font), TRUE);
  ApplyTheme();
}

LRESULT CALLBACK PinsWndProc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam) {
  switch (message) {
    case WM_CREATE: {
      w.hwnd = hwnd;
      w.dpi = GetDpiForWindow(hwnd);
      auto instance = reinterpret_cast<CREATESTRUCTW*>(lparam)->hInstance;
      w.hint = CreateWindowW(L"STATIC", kHint, WS_CHILD | WS_VISIBLE | SS_LEFT | SS_NOPREFIX, 0, 0,
                             0, 0, hwnd, nullptr, instance, nullptr);
      w.alignmentLabel = CreateWindowW(L"STATIC", kAlignmentLabel,
                                       WS_CHILD | WS_VISIBLE | SS_LEFT | SS_NOPREFIX | SS_CENTERIMAGE,
                                       0, 0, 0, 0, hwnd, nullptr, instance, nullptr);
      w.list = CreateWindowExW(0, WC_LISTVIEWW, L"",
                               WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_BORDER | LVS_REPORT |
                                   LVS_SINGLESEL | LVS_SHOWSELALWAYS | LVS_NOSORTHEADER,
                               0, 0, 0, 0, hwnd, reinterpret_cast<HMENU>(kListId), instance, nullptr);
      ListView_SetExtendedListViewStyle(w.list, LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER);
      SetWindowSubclass(w.list, ListSubclassProc, 0, 0);
      w.reset = CreateWindowW(L"BUTTON", kResetText, WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
                              0, 0, 0, 0, hwnd, reinterpret_cast<HMENU>(kResetId), instance, nullptr);
      w.close = CreateWindowW(L"BUTTON", L"Close", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
                              0, 0, 0, 0, hwnd, reinterpret_cast<HMENU>(IDCANCEL), instance, nullptr);
      ApplyDpi();
      Reload();
      // The list header only gets its height once the list has a size.
      SetWindowPos(hwnd, nullptr, 0, 0, Scale(800), Scale(600), SWP_NOMOVE | SWP_NOZORDER);
      SizeToContent();
      return 0;
    }
    case WM_SIZE:
      Layout();
      return 0;
    case WM_GETMINMAXINFO: {
      auto* info = reinterpret_cast<MINMAXINFO*>(lparam);
      info->ptMinTrackSize = {Scale(420), Scale(260)};
      return 0;
    }
    case WM_DPICHANGED: {
      w.dpi = HIWORD(wparam);
      ApplyDpi();
      FillList();
      UpdateShown();
      const RECT* suggested = reinterpret_cast<RECT*>(lparam);
      SetWindowPos(hwnd, nullptr, suggested->left, suggested->top, suggested->right - suggested->left,
                   suggested->bottom - suggested->top, SWP_NOZORDER | SWP_NOACTIVATE);
      return 0;
    }
    case WM_THEMECHANGED:
      ApplyTheme();
      return 0;
    case WM_SETTINGCHANGE:
      if (lparam && lstrcmpiW(reinterpret_cast<LPCWSTR>(lparam), L"ImmersiveColorSet") == 0) {
        ApplyTheme();
        FillList();  // Store app icons come in light and dark versions.
        UpdateShown();
      }
      return 0;
    case WM_ERASEBKGND: {
      RECT client;
      GetClientRect(hwnd, &client);
      FillRect(reinterpret_cast<HDC>(wparam), &client, w.backgroundBrush);
      return 1;
    }
    case WM_CTLCOLORBTN:
      return reinterpret_cast<LRESULT>(w.backgroundBrush);
    case WM_CTLCOLORLISTBOX:  // The dropdowns' open lists.
      SetTextColor(reinterpret_cast<HDC>(wparam), w.text);
      SetBkColor(reinterpret_cast<HDC>(wparam), w.background);
      return reinterpret_cast<LRESULT>(w.backgroundBrush);
    case WM_APP_LAYOUT:
      Layout();
      return 0;
    case WM_ACTIVATE:
      // Pins may have changed while the window was in the background.
      if (LOWORD(wparam) != WA_INACTIVE) Reload();
      return 0;
    case WM_DISPLAYCHANGE:
      Reload();
      return 0;
    case WM_CTLCOLORSTATIC:
      SetBkMode(reinterpret_cast<HDC>(wparam), TRANSPARENT);
      SetTextColor(reinterpret_cast<HDC>(wparam), w.text);
      return reinterpret_cast<LRESULT>(w.backgroundBrush);
    case WM_NOTIFY: {
      auto* header = reinterpret_cast<NMHDR*>(lparam);
      if (header->hwndFrom != w.list) break;
      if (header->code == NM_CUSTOMDRAW) {
        return OnListCustomDraw(reinterpret_cast<NMLVCUSTOMDRAW*>(lparam));
      }
      // The second click of a double click only arrives as NM_DBLCLK.
      if (header->code == NM_CLICK || header->code == NM_DBLCLK) {
        OnListClick(reinterpret_cast<NMITEMACTIVATE*>(lparam)->ptAction);
      }
      return 0;
    }
    case WM_COMMAND:
      if (HIWORD(wparam) == CBN_SELCHANGE && LOWORD(wparam) >= kAlignmentIdBase) {
        OnAlignmentChosen(LOWORD(wparam) - kAlignmentIdBase);
        return 0;
      }
      switch (LOWORD(wparam)) {
        case kResetId:
          Log(L"Putting every pin back where Windows puts it");
          ClearPinMonitors();
          w.onChanged();
          Reload();
          return 0;
        case IDOK:
        case IDCANCEL:
          DestroyWindow(hwnd);
          return 0;
      }
      break;
    case WM_DESTROY:
      if (w.checkTheme) CloseThemeData(w.checkTheme);
      if (w.font) DeleteObject(w.font);
      if (w.backgroundBrush) DeleteObject(w.backgroundBrush);
      w = {};
      return 0;
  }
  return DefWindowProcW(hwnd, message, wparam, lparam);
}

}  // namespace

void ShowPinsWindow(HINSTANCE instance, std::function<void()> onChanged) {
  if (w.hwnd) {
    ShowWindow(w.hwnd, SW_RESTORE);
    SetForegroundWindow(w.hwnd);
    return;
  }
  static bool registered = false;
  if (!registered) {
    INITCOMMONCONTROLSEX controls = {sizeof(controls), ICC_LISTVIEW_CLASSES | ICC_STANDARD_CLASSES};
    InitCommonControlsEx(&controls);
    WNDCLASSEXW wc = {sizeof(wc)};
    wc.lpfnWndProc = PinsWndProc;
    wc.hInstance = instance;
    wc.hIcon = LoadIconW(instance, MAKEINTRESOURCEW(1));
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
    wc.lpszClassName = kWindowClass;
    registered = RegisterClassExW(&wc) != 0;
  }
  w.onChanged = std::move(onChanged);

  // Open on the monitor the tray icon was clicked on.
  POINT cursor;
  GetCursorPos(&cursor);
  MONITORINFO info = {sizeof(info)};
  GetMonitorInfoW(MonitorFromPoint(cursor, MONITOR_DEFAULTTONEAREST), &info);
  HWND hwnd = CreateWindowExW(WS_EX_CONTROLPARENT, kWindowClass, L"Unglom: pinned apps on each monitor",
                              WS_OVERLAPPEDWINDOW, info.rcWork.left, info.rcWork.top, 400, 300, nullptr,
                              nullptr, instance, nullptr);
  if (!hwnd) {
    Log(L"Creating the pinned apps window failed: %lu", GetLastError());
    return;
  }
  ShowWindow(hwnd, SW_SHOW);
  SetForegroundWindow(hwnd);
}

bool IsPinsWindowMessage(MSG* msg) { return w.hwnd && IsDialogMessageW(w.hwnd, msg); }

}  // namespace unglom
