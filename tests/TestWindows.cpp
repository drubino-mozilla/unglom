// Opens one window per title argument so Unglom has a multi-window app to
// label, then exits. A title written "Before=>After" is renamed halfway through.
//
// Usage: UnglomTestWindows.exe [--appid <id>] [--at <x>,<y>] <seconds> <title> [<title> ...]
//   --appid  gives the windows another app's AppUserModelID, e.g. a pinned app's
//   --at     places the first window there (unscaled coordinates) instead of 100,100

#include <windows.h>
#include <shellapi.h>
#include <shobjidl.h>

#include <string>
#include <vector>

namespace {

struct TestWindow {
  HWND hwnd;
  std::wstring renameTo;
};

std::vector<TestWindow> g_windows;
constexpr UINT_PTR kRenameTimer = 1;
constexpr UINT_PTR kExitTimer = 2;

LRESULT CALLBACK WndProc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam) {
  if (message == WM_TIMER && wparam == kRenameTimer) {
    KillTimer(hwnd, kRenameTimer);
    for (const TestWindow& w : g_windows) {
      if (!w.renameTo.empty()) SetWindowTextW(w.hwnd, w.renameTo.c_str());
    }
    return 0;
  }
  if (message == WM_TIMER && wparam == kExitTimer) {
    PostQuitMessage(0);
    return 0;
  }
  if (message == WM_CLOSE) {
    PostQuitMessage(0);
    return 0;
  }
  return DefWindowProcW(hwnd, message, wparam, lparam);
}

}  // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int) {
  int argc = 0;
  LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
  int first = 1;
  int x = 100, y = 100;
  for (; first + 1 < argc; first += 2) {
    std::wstring option = argv[first];
    if (option == L"--appid") {
      SetCurrentProcessExplicitAppUserModelID(argv[first + 1]);
    } else if (option == L"--at") {
      if (swscanf_s(argv[first + 1], L"%d,%d", &x, &y) != 2) return 1;
    } else {
      break;
    }
  }
  if (argc < first + 2) return 1;
  UINT seconds = static_cast<UINT>(_wtoi(argv[first]));

  WNDCLASSW wc = {};
  wc.lpfnWndProc = WndProc;
  wc.hInstance = instance;
  wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
  wc.hIcon = LoadIconW(nullptr, IDI_INFORMATION);
  wc.lpszClassName = L"UnglomTestWindow";
  RegisterClassW(&wc);

  for (int i = first + 1; i < argc; ++i) {
    std::wstring title = argv[i];
    std::wstring renameTo;
    size_t arrow = title.find(L"=>");
    if (arrow != std::wstring::npos) {
      renameTo = title.substr(arrow + 2);
      title = title.substr(0, arrow);
    }
    int offset = (i - first - 1) * 40;
    HWND hwnd = CreateWindowW(wc.lpszClassName, title.c_str(), WS_OVERLAPPEDWINDOW, x + offset,
                              y + offset, 400, 200, nullptr, nullptr, instance, nullptr);
    ShowWindow(hwnd, SW_SHOWNOACTIVATE);
    g_windows.push_back({hwnd, renameTo});
  }
  LocalFree(argv);

  SetTimer(g_windows[0].hwnd, kRenameTimer, seconds * 500, nullptr);
  SetTimer(g_windows[0].hwnd, kExitTimer, seconds * 1000, nullptr);

  MSG msg;
  while (GetMessageW(&msg, nullptr, 0, 0)) {
    TranslateMessage(&msg);
    DispatchMessageW(&msg);
  }
  return 0;
}
