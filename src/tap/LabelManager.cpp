#include "LabelManager.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <limits>
#include <map>
#include <mutex>
#include <string_view>
#include <vector>

#include "../common/AppResolver.h"
#include "../common/Log.h"
#include "../common/TitleDiff.h"
#include "AppIdentity.h"

namespace xaml = winrt::Windows::UI::Xaml;
using namespace std::chrono_literals;
using winrt::Windows::System::DispatcherQueue;
using winrt::Windows::System::DispatcherQueuePriority;
using winrt::Windows::UI::Xaml::Automation::AutomationProperties;
using winrt::Windows::UI::Xaml::Automation::Peers::FrameworkElementAutomationPeer;
using winrt::Windows::UI::Xaml::Controls::TextBlock;
using winrt::Windows::UI::Xaml::Media::VisualTreeHelper;

namespace unglom {
namespace {

constexpr std::wstring_view kButtonType = L"Taskbar.TaskListButton";
constexpr std::wstring_view kLabelType = L"Windows.UI.Xaml.Controls.TextBlock";
constexpr std::wstring_view kLabelName = L"LabelControl";
constexpr std::wstring_view kXamlSourceType = L"Windows.UI.Xaml.Hosting.DesktopWindowXamlSource";
constexpr std::wstring_view kWindowIdPrefix = L"Window: ";
constexpr std::wstring_view kPinIdPrefix = L"Appid: ";
constexpr ULONGLONG kAppIdRefreshMs = 2'000;

std::mutex g_mutex;
std::map<DWORD, LabelManager*> g_managers;
std::atomic<bool> g_active{true};
Mode g_mode = Mode::Run;
winrt::com_ptr<IXamlDiagnostics> g_diagnostics;

struct SharedSettings {
  PinSettings pins;
  std::set<std::wstring> pinnedApps;
};
std::mutex g_settingsMutex;
SharedSettings g_settings;
std::atomic<int> g_settingsVersion{0};

std::wstring_view ClassName(xaml::DependencyObject const& o, winrt::hstring& storage) {
  storage = winrt::get_class_name(o);
  return storage;
}

xaml::FrameworkElement FindButton(xaml::DependencyObject o) {
  winrt::hstring name;
  while (o) {
    if (ClassName(o, name) == kButtonType) return o.try_as<xaml::FrameworkElement>();
    o = VisualTreeHelper::GetParent(o);
  }
  return nullptr;
}

xaml::DependencyObject FindRoot(xaml::DependencyObject o) {
  xaml::DependencyObject root = o;
  while (o) {
    root = o;
    o = VisualTreeHelper::GetParent(o);
  }
  return root;
}

// The attached property is set on task list buttons and, unlike the automation
// peer, still answers while the button is collapsed.
std::wstring AutomationId(xaml::FrameworkElement const& element) {
  winrt::hstring id = AutomationProperties::GetAutomationId(element);
  if (!id.empty()) return std::wstring(id);
  auto peer = FrameworkElementAutomationPeer::FromElement(element);
  if (!peer) peer = FrameworkElementAutomationPeer::CreatePeerForElement(element);
  return peer ? std::wstring(peer.GetAutomationId()) : std::wstring();
}

// The task list's ItemsRepeater keeps recycled buttons in the tree, parked
// around (-10000, -10000), and they still report their previous window.
bool IsParked(xaml::UIElement const& button) {
  auto offset = button.ActualOffset();
  return offset.x < -5000 || offset.y < -5000;
}

HWND WindowForAutomationId(const std::wstring& id) {
  if (id.rfind(kWindowIdPrefix, 0) != 0) return nullptr;
  auto value = wcstoull(id.c_str() + kWindowIdPrefix.size(), nullptr, 16);
  return reinterpret_cast<HWND>(static_cast<uintptr_t>(value));
}

HWND WindowForButton(xaml::FrameworkElement const& button) {
  return WindowForAutomationId(AutomationId(button));
}

void* Identity(winrt::Windows::Foundation::IInspectable const& o) {
  return winrt::get_abi(o.as<winrt::Windows::Foundation::IUnknown>());
}

void* RootKey(xaml::DependencyObject const& o) { return Identity(FindRoot(o)); }

// Minimized windows stay on the taskbar of the monitor they were minimized from.
HMONITOR MonitorForWindow(HWND hwnd) {
  WINDOWPLACEMENT placement = {sizeof(placement)};
  if (IsIconic(hwnd) && GetWindowPlacement(hwnd, &placement)) {
    return MonitorFromRect(&placement.rcNormalPosition, MONITOR_DEFAULTTONEAREST);
  }
  return MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST);
}

xaml::UIElement FindNamed(xaml::DependencyObject const& o, std::wstring_view name) {
  if (auto fe = o.try_as<xaml::FrameworkElement>(); fe && fe.Name() == name) return fe;
  int n = VisualTreeHelper::GetChildrenCount(o);
  for (int i = 0; i < n; ++i) {
    if (auto found = FindNamed(VisualTreeHelper::GetChild(o, i), name)) return found;
  }
  return nullptr;
}

std::wstring Describe(xaml::DependencyObject const& o) {
  winrt::hstring storage;
  std::wstring s{ClassName(o, storage)};
  if (auto fe = o.try_as<xaml::FrameworkElement>()) {
    if (!fe.Name().empty()) s += L"#" + std::wstring(fe.Name());
    auto m = fe.Margin();
    wchar_t buf[256];
    swprintf_s(buf, L" [%.1fx%.1f w=%g minw=%g maxw=%g vis=%d margin=%g,%g,%g,%g]",
               fe.ActualWidth(), fe.ActualHeight(), fe.Width(), fe.MinWidth(), fe.MaxWidth(),
               static_cast<int>(fe.Visibility()), m.Left, m.Top, m.Right, m.Bottom);
    s += buf;
  }
  if (auto tb = o.try_as<TextBlock>()) {
    s += L" text=\"" + std::wstring(tb.Text()) + L"\"";
    if (tb.GetBindingExpression(TextBlock::TextProperty())) s += L" (Binding)";
  }
  return s;
}

void DumpTree(xaml::DependencyObject const& o, int depth) {
  Log(L"%*ls%ls", depth * 2, L"", Describe(o).c_str());
  int n = VisualTreeHelper::GetChildrenCount(o);
  for (int i = 0; i < n; ++i) DumpTree(VisualTreeHelper::GetChild(o, i), depth + 1);
}

template <typename F>
void Guarded(const wchar_t* what, F&& f) {
  try {
    f();
  } catch (winrt::hresult_error const& e) {
    Log(L"%ls failed: 0x%08X %ls", what, static_cast<unsigned>(e.code().value), e.message().c_str());
  } catch (std::exception const& e) {
    Log(L"%ls failed: %hs", what, e.what());
  }
}

}  // namespace

void LabelManager::Configure(Mode mode, winrt::com_ptr<IXamlDiagnostics> diagnostics) {
  g_mode = mode;
  g_diagnostics = std::move(diagnostics);
}

LabelManager& LabelManager::ForCurrentThread() {
  std::lock_guard lock(g_mutex);
  LabelManager*& manager = g_managers[GetCurrentThreadId()];
  if (!manager) manager = new LabelManager();  // Lives as long as the UI thread; never freed.
  return *manager;
}

bool LabelManager::IsActive() { return g_active; }

void LabelManager::DeactivateAll() {
  g_active = false;
  std::lock_guard lock(g_mutex);
  for (auto& [thread, manager] : g_managers) {
    if (!manager->queue_) continue;
    manager->queue_.TryEnqueue([manager] { Guarded(L"RestoreAll", [&] { manager->RestoreAll(); }); });
  }
}

void LabelManager::ReloadSettings() {
  SharedSettings loaded;
  loaded.pins = LoadPinSettings();
  for (const PinnedApp& pin : ShortcutPins()) loaded.pinnedApps.insert(pin.appId);
  for (const std::wstring& appId : StorePins()) loaded.pinnedApps.insert(appId);
  Log(L"Settings: %ls, %zu pins assigned to monitors, %zu pinned apps",
      loaded.pins.takenOver ? L"placing buttons per monitor" : L"leaving buttons alone",
      loaded.pins.pinMonitors.size(), loaded.pinnedApps.size());
  {
    std::lock_guard lock(g_settingsMutex);
    g_settings = std::move(loaded);
    ++g_settingsVersion;
  }
  std::lock_guard lock(g_mutex);
  for (auto& [thread, manager] : g_managers) {
    if (manager->queue_) manager->queue_.TryEnqueue([manager] { manager->ScheduleRecompute(); });
  }
}

LabelManager::LabelManager() : queue_(DispatcherQueue::GetForCurrentThread()) {
  if (!queue_) Log(L"No DispatcherQueue on this thread; labels here will not be managed");
}

void LabelManager::OnElementAdded(InstanceHandle handle, const std::wstring& type,
                                  const std::wstring& name) {
  bool isButton = type == kButtonType;
  bool isLabel = type == kLabelType && name == kLabelName;
  bool isSource = type == kXamlSourceType;
  if (stopped_ || !queue_ || (!isButton && !isLabel && !isSource)) return;

  winrt::Windows::Foundation::IInspectable object;
  if (FAILED(g_diagnostics->GetIInspectableFromHandle(
          handle, reinterpret_cast<::IInspectable**>(winrt::put_abi(object))))) {
    return;
  }

  if (isSource) {
    // Each taskbar is a XAML island in its own window; this tells us which.
    sources_.push_back(winrt::make_weak(object.as<xaml::Hosting::DesktopWindowXamlSource>()));
    return;
  }

  if (isButton) {
    buttons_[handle] = winrt::make_weak(object.as<xaml::FrameworkElement>());
    if (g_mode == Mode::Dump) ScheduleDump();
    if (g_mode == Mode::Run) {
      EnsureTimer();
      ScheduleRecompute();
    }
    return;
  }
  if (g_mode != Mode::Run) return;

  auto label = object.as<TextBlock>();
  auto onChange = [this](auto&&, auto&&) {
    if (!writing_) ScheduleRecompute();
  };
  TrackedLabel tracked;
  tracked.label = winrt::make_weak(label);
  tracked.textToken = label.RegisterPropertyChangedCallback(TextBlock::TextProperty(), onChange);
  tracked.visibilityToken =
      label.RegisterPropertyChangedCallback(xaml::UIElement::VisibilityProperty(), onChange);
  if (auto panel = VisualTreeHelper::GetParent(label).try_as<xaml::FrameworkElement>()) {
    tracked.panel = winrt::make_weak(panel);
    tracked.widthToken =
        panel.RegisterPropertyChangedCallback(xaml::FrameworkElement::WidthProperty(), onChange);
  }
  labels_[handle] = tracked;

  EnsureTimer();
  ScheduleRecompute();
}

void LabelManager::EnsureTimer() {
  if (timer_) return;
  // Catches title changes the taskbar does not push into the label, and windows
  // moving between monitors.
  timer_ = queue_.CreateTimer();
  timer_.Interval(1s);
  timer_.IsRepeating(true);
  timer_.Tick([this](auto&&, auto&&) { Guarded(L"Recompute", [&] { Recompute(); }); });
  timer_.Start();
}

void LabelManager::OnElementRemoved(InstanceHandle handle) {
  // hidden_ and hiddenIndicators_ keep their entries: the task list takes
  // buttons out and puts them back, and they come back still hidden.
  buttons_.erase(handle);
  auto it = labels_.find(handle);
  if (it == labels_.end()) return;
  Unregister(it->second);
  labels_.erase(it);
  ScheduleRecompute();
}

void LabelManager::Unregister(TrackedLabel& tracked) {
  if (auto label = tracked.label.get()) {
    label.UnregisterPropertyChangedCallback(TextBlock::TextProperty(), tracked.textToken);
    label.UnregisterPropertyChangedCallback(xaml::UIElement::VisibilityProperty(),
                                            tracked.visibilityToken);
  }
  if (auto panel = tracked.panel.get()) {
    panel.UnregisterPropertyChangedCallback(xaml::FrameworkElement::WidthProperty(),
                                            tracked.widthToken);
  }
}

void LabelManager::ScheduleRecompute() {
  if (recomputePending_ || stopped_ || !queue_) return;
  recomputePending_ = true;
  queue_.TryEnqueue(DispatcherQueuePriority::Low, [this] {
    recomputePending_ = false;
    Guarded(L"Recompute", [&] { Recompute(); });
  });
}

void LabelManager::Recompute() {
  if (stopped_ || g_mode != Mode::Run) return;

  if (settingsVersion_ != g_settingsVersion) {
    std::lock_guard lock(g_settingsMutex);
    settings_ = g_settings.pins;
    pinnedApps_ = g_settings.pinnedApps;
    settingsVersion_ = g_settingsVersion;
  }
  if (settingsVersion_ > 0) PlaceButtons();

  // Buttons without a window (pinned apps) show how wide the taskbar makes icon-only buttons.
  for (auto& [handle, weak] : buttons_) {
    auto button = weak.get();
    if (!button || VisualTreeHelper::GetChildrenCount(button) == 0) continue;
    auto panel = VisualTreeHelper::GetChild(button, 0).try_as<xaml::FrameworkElement>();
    if (panel && !std::isnan(panel.Width()) && panel.Width() > 0 && !WindowForButton(button)) {
      iconOnlyWidth_ = panel.Width();
      break;
    }
  }

  struct Entry {
    TrackedLabel* tracked;
    TextBlock label;
    std::wstring title;
  };
  std::map<std::wstring, std::vector<Entry>> groups;

  for (auto it = labels_.begin(); it != labels_.end();) {
    TextBlock label = it->second.label.get();
    if (!label) {
      it = labels_.erase(it);
      continue;
    }
    TrackedLabel& tracked = it->second;
    ++it;

    auto button = FindButton(label);
    if (!button || IsParked(button) || button.Visibility() == xaml::Visibility::Collapsed) continue;
    HWND hwnd = WindowForButton(button);
    if (!hwnd || !IsWindow(hwnd)) continue;

    // Group per taskbar (each monitor has its own visual tree root) and per app.
    // A button standing in for a pin is icon-only, like the pin.
    wchar_t rootId[32];
    swprintf_s(rootId, L"%p|", RootKey(button));
    std::wstring appKey;
    if (standIns_.count(Identity(button))) {
      wchar_t own[32];
      swprintf_s(own, L"pin:%p", Identity(button));
      appKey = own;
    } else {
      appKey = AppKeyForWindow(hwnd);
    }
    groups[rootId + appKey].push_back({&tracked, label, WindowTitle(hwnd)});
  }

  for (auto& [key, entries] : groups) {
    std::vector<std::wstring> titles;
    for (const Entry& e : entries) titles.push_back(e.title);
    std::vector<std::wstring> texts = DistinctLabels(titles);
    for (size_t i = 0; i < entries.size(); ++i) Apply(*entries[i].tracked, entries[i].label, texts[i]);
  }
}

// While Unglom has switched Windows to "All taskbars", every taskbar has a button
// for every window and pin. Show each window only where the user's own setting
// would, and each pin on the monitors chosen for it.
void LabelManager::PlaceButtons() {
  if (!settings_.takenOver) {
    ShowAll();
    return;
  }

  std::map<void*, std::vector<PlacedButton>> roots;
  for (auto& [handle, weak] : buttons_) {
    auto button = weak.get();
    if (!button || IsParked(button)) continue;
    std::wstring id = AutomationId(button);
    PlacedButton placed{button};
    if (id.rfind(kPinIdPrefix, 0) == 0) {
      placed.appId = Lowercase(id.substr(kPinIdPrefix.size()));
    } else if (HWND hwnd = WindowForAutomationId(id); hwnd && IsWindow(hwnd)) {
      placed.hwnd = hwnd;
      placed.appId = AppIdForWindow(hwnd);
    } else {
      continue;
    }
    roots[RootKey(button)].push_back(std::move(placed));
  }

  standIns_.clear();
  std::erase_if(hidden_, [](const auto& entry) { return !entry.second.get(); });
  std::erase_if(hiddenIndicators_, [](const auto& entry) { return !entry.second.get(); });
  std::vector<Monitor> monitors = ConnectedMonitors();
  HMONITOR primary = MonitorFromPoint({0, 0}, MONITOR_DEFAULTTOPRIMARY);
  auto isPinned = [&](const std::wstring& appId) { return pinnedApps_.count(appId) > 0; };
  auto isConnected = [&](const std::wstring& monitorId) {
    return std::any_of(monitors.begin(), monitors.end(),
                       [&](const Monitor& m) { return m.id == monitorId; });
  };

  for (auto& [root, placed] : roots) {
    HMONITOR monitor = RootMonitor(root);
    auto here = std::find_if(monitors.begin(), monitors.end(),
                             [&](const Monitor& m) { return m.handle == monitor; });
    if (here == monitors.end()) continue;

    auto pinShowsHere = [&](const std::wstring& appId) {
      auto it = settings_.pinMonitors.find(appId);
      if (it != settings_.pinMonitors.end()) {
        if (std::find(it->second.begin(), it->second.end(), here->id) != it->second.end()) return true;
        if (std::any_of(it->second.begin(), it->second.end(), isConnected)) return false;
      }
      return settings_.windows == TaskbarApps::AllTaskbars || monitor == primary;
    };
    auto windowShowsHere = [&](HWND hwnd) {
      return settings_.windows == TaskbarApps::AllTaskbars || MonitorForWindow(hwnd) == monitor ||
             (settings_.windows == TaskbarApps::MainAndWhereOpen && monitor == primary);
    };

    std::sort(placed.begin(), placed.end(), [](const PlacedButton& a, const PlacedButton& b) {
      return a.button.ActualOffset().x < b.button.ActualOffset().x;
    });
    std::vector<bool> windowHere(placed.size());
    std::set<std::wstring> appsHere;
    for (size_t i = 0; i < placed.size(); ++i) {
      if (placed[i].hwnd && windowShowsHere(placed[i].hwnd)) {
        windowHere[i] = true;
        appsHere.insert(placed[i].appId);
      }
    }
    for (size_t i = 0; i < placed.size(); ++i) {
      const PlacedButton& p = placed[i];
      if (!p.hwnd) {
        Show(p, pinShowsHere(p.appId), false);
      } else if (windowHere[i]) {
        Show(p, true, false);
      } else {
        // A pinned app running only on other monitors keeps its pin here, the
        // way Windows shows it; its first button sits where the pin goes.
        bool asPin = isPinned(p.appId) && pinShowsHere(p.appId) && appsHere.insert(p.appId).second;
        Show(p, asPin, asPin);
      }
    }
  }
}

HMONITOR LabelManager::RootMonitor(void* root) {
  HWND& window = rootWindows_[root];
  if (!window || !IsWindow(window)) {
    window = nullptr;
    std::erase_if(sources_, [](const auto& weak) { return !weak.get(); });
    for (const auto& weak : sources_) {
      auto source = weak.get();
      auto content = source ? source.Content() : nullptr;
      if (!content || RootKey(content) != root) continue;
      source.as<IDesktopWindowXamlSourceNative>()->get_WindowHandle(&window);
      wchar_t className[64] = {};
      GetClassNameW(GetAncestor(window, GA_ROOT), className, ARRAYSIZE(className));
      Log(L"Taskbar %p is in window %p (%ls)", root, window, className);
      break;
    }
  }
  return window ? MonitorFromWindow(window, MONITOR_DEFAULTTONULL) : nullptr;
}

std::wstring LabelManager::AppIdForWindow(HWND hwnd) {
  ULONGLONG now = GetTickCount64();
  CachedAppId& cached = appIds_[hwnd];
  if (cached.appId.empty() || now - cached.at > kAppIdRefreshMs) {
    cached.appId = TaskbarAppIdForWindow(hwnd);
    cached.at = now;
  }
  if (appIds_.size() > 500) {
    std::erase_if(appIds_, [&](const auto& entry) { return now - entry.second.at > kAppIdRefreshMs; });
  }
  return cached.appId;
}

void LabelManager::Show(const PlacedButton& placed, bool show, bool asPin) {
  void* key = Identity(placed.button);
  if (!show) {
    if (placed.button.Visibility() != xaml::Visibility::Collapsed) {
      placed.button.Visibility(xaml::Visibility::Collapsed);
    }
    hidden_[key] = winrt::make_weak(placed.button);
  } else if (hidden_.erase(key)) {
    placed.button.Visibility(xaml::Visibility::Visible);
  }

  if (asPin) standIns_.insert(key);
  auto indicator = hiddenIndicators_.find(key);
  if (asPin && indicator == hiddenIndicators_.end()) {
    if (auto element = FindNamed(placed.button, L"RunningIndicator")) {
      element.Opacity(0);
      hiddenIndicators_[key] = winrt::make_weak(element);
    }
  } else if (!asPin && indicator != hiddenIndicators_.end()) {
    if (auto element = indicator->second.get()) element.ClearValue(xaml::UIElement::OpacityProperty());
    hiddenIndicators_.erase(indicator);
  }
}

void LabelManager::ShowAll() {
  for (auto& [key, weak] : hidden_) {
    if (auto button = weak.get()) button.Visibility(xaml::Visibility::Visible);
  }
  hidden_.clear();
  for (auto& [key, weak] : hiddenIndicators_) {
    if (auto element = weak.get()) element.ClearValue(xaml::UIElement::OpacityProperty());
  }
  hiddenIndicators_.clear();
  standIns_.clear();
}

void LabelManager::Apply(TrackedLabel& tracked, TextBlock const& label, const std::wstring& text) {
  bool iconOnly = text.empty();
  auto visibility = iconOnly ? xaml::Visibility::Collapsed : xaml::Visibility::Visible;
  auto panel = tracked.panel.get();
  double width = iconOnly ? iconOnlyWidth_ : std::numeric_limits<double>::quiet_NaN();

  bool textOk = iconOnly || label.Text() == text;
  bool widthOk = !panel || (iconOnly ? panel.Width() == width : std::isnan(panel.Width()));
  if (label.Visibility() == visibility && textOk && widthOk) return;

  Log(L"\"%ls\" -> %ls", label.Text().c_str(), iconOnly ? L"(icon only)" : text.c_str());
  writing_ = true;
  Guarded(L"Apply", [&] {
    if (!textOk) label.Text(text);
    label.Visibility(visibility);
    if (panel && !widthOk) panel.Width(width);
  });
  writing_ = false;
  tracked.modified = true;
}

void LabelManager::RestoreAll() {
  stopped_ = true;
  if (timer_) timer_.Stop();
  if (dumpTimer_) dumpTimer_.Stop();

  int restored = 0;
  for (auto& [handle, tracked] : labels_) {
    TextBlock label = tracked.label.get();
    if (!label) continue;
    Guarded(L"Restore", [&] {
      Unregister(tracked);
      if (!tracked.modified) return;
      writing_ = true;
      label.Visibility(xaml::Visibility::Visible);
      if (auto panel = tracked.panel.get()) panel.Width(std::numeric_limits<double>::quiet_NaN());
      if (auto button = FindButton(label)) {
        if (HWND hwnd = WindowForButton(button)) label.Text(WindowTitle(hwnd));
      }
      writing_ = false;
      ++restored;
    });
  }
  writing_ = false;
  labels_.clear();
  size_t hidden = hidden_.size();
  Guarded(L"ShowAll", [&] { ShowAll(); });
  buttons_.clear();
  Log(L"Restored %d labels and %zu hidden buttons", restored, hidden);
}

void LabelManager::ScheduleDump() {
  if (!dumpTimer_) {
    dumpTimer_ = queue_.CreateTimer();
    dumpTimer_.Interval(3s);
    dumpTimer_.IsRepeating(false);
    dumpTimer_.Tick([this](auto&&, auto&&) { Guarded(L"Dump", [&] { Dump(); }); });
  }
  dumpTimer_.Stop();
  dumpTimer_.Start();
}

void LabelManager::Dump() {
  Log(L"=== Dump: %zu task list buttons on this thread ===", buttons_.size());
  bool first = true;
  for (auto& [handle, weak] : buttons_) {
    auto button = weak.get();
    if (!button) continue;
    Log(L"--- automationId=\"%ls\"", AutomationId(button).c_str());
    if (first) {
      Log(L"Ancestors:");
      for (auto o = VisualTreeHelper::GetParent(button); o; o = VisualTreeHelper::GetParent(o)) {
        Log(L"  ^ %ls", Describe(o).c_str());
      }
      first = false;
    }
    DumpTree(button, 1);
  }
  Log(L"=== End dump ===");
}

}  // namespace unglom
