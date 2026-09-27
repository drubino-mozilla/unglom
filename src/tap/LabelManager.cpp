#include "LabelManager.h"

#include <atomic>
#include <cmath>
#include <limits>
#include <map>
#include <mutex>
#include <string_view>
#include <vector>

#include "../common/Log.h"
#include "../common/TitleDiff.h"
#include "AppIdentity.h"

namespace xaml = winrt::Windows::UI::Xaml;
using namespace std::chrono_literals;
using winrt::Windows::System::DispatcherQueue;
using winrt::Windows::System::DispatcherQueuePriority;
using winrt::Windows::UI::Xaml::Automation::Peers::FrameworkElementAutomationPeer;
using winrt::Windows::UI::Xaml::Controls::TextBlock;
using winrt::Windows::UI::Xaml::Media::VisualTreeHelper;

namespace unglom {
namespace {

constexpr std::wstring_view kButtonType = L"Taskbar.TaskListButton";
constexpr std::wstring_view kLabelType = L"Windows.UI.Xaml.Controls.TextBlock";
constexpr std::wstring_view kLabelName = L"LabelControl";
constexpr std::wstring_view kWindowIdPrefix = L"Window: ";

std::mutex g_mutex;
std::map<DWORD, LabelManager*> g_managers;
std::atomic<bool> g_active{true};
Mode g_mode = Mode::Run;
winrt::com_ptr<IXamlDiagnostics> g_diagnostics;

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

std::wstring AutomationId(xaml::FrameworkElement const& element) {
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

HWND WindowForButton(xaml::FrameworkElement const& button) {
  std::wstring id = AutomationId(button);
  if (id.rfind(kWindowIdPrefix, 0) != 0) return nullptr;
  auto value = wcstoull(id.c_str() + kWindowIdPrefix.size(), nullptr, 16);
  return reinterpret_cast<HWND>(static_cast<uintptr_t>(value));
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

LabelManager::LabelManager() : queue_(DispatcherQueue::GetForCurrentThread()) {
  if (!queue_) Log(L"No DispatcherQueue on this thread; labels here will not be managed");
}

void LabelManager::OnElementAdded(InstanceHandle handle, const std::wstring& type,
                                  const std::wstring& name) {
  bool isButton = type == kButtonType;
  bool isLabel = type == kLabelType && name == kLabelName;
  if (stopped_ || !queue_ || (!isButton && !isLabel)) return;

  winrt::Windows::Foundation::IInspectable object;
  if (FAILED(g_diagnostics->GetIInspectableFromHandle(
          handle, reinterpret_cast<::IInspectable**>(winrt::put_abi(object))))) {
    return;
  }

  if (isButton) {
    buttons_[handle] = winrt::make_weak(object.as<xaml::FrameworkElement>());
    if (g_mode == Mode::Dump) ScheduleDump();
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

  if (!timer_) {
    // Catches title changes the taskbar does not push into the label.
    timer_ = queue_.CreateTimer();
    timer_.Interval(1s);
    timer_.IsRepeating(true);
    timer_.Tick([this](auto&&, auto&&) { Guarded(L"Recompute", [&] { Recompute(); }); });
    timer_.Start();
  }
  ScheduleRecompute();
}

void LabelManager::OnElementRemoved(InstanceHandle handle) {
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
    if (!button || IsParked(button)) continue;
    HWND hwnd = WindowForButton(button);
    if (!hwnd || !IsWindow(hwnd)) continue;

    // Group per taskbar (each monitor has its own visual tree root) and per app.
    auto root = FindRoot(button).as<winrt::Windows::Foundation::IUnknown>();
    wchar_t rootId[32];
    swprintf_s(rootId, L"%p|", winrt::get_abi(root));
    groups[rootId + AppKeyForWindow(hwnd)].push_back({&tracked, label, WindowTitle(hwnd)});
  }

  for (auto& [key, entries] : groups) {
    std::vector<std::wstring> titles;
    for (const Entry& e : entries) titles.push_back(e.title);
    std::vector<std::wstring> texts = DistinctLabels(titles);
    for (size_t i = 0; i < entries.size(); ++i) Apply(*entries[i].tracked, entries[i].label, texts[i]);
  }
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
  buttons_.clear();
  Log(L"Restored %d labels", restored);
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
