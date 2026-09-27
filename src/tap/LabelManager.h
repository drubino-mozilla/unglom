#pragma once

#include "TapCommon.h"

#include <set>
#include <string>
#include <unordered_map>
#include <vector>

#include "../common/PinSettings.h"

namespace unglom {

enum class Mode {
  Dump,  // Only log the taskbar's visual tree; change nothing.
  Run,
};

// Owns the taskbar label rewriting, and which buttons show on which monitor's
// taskbar, for one XAML UI thread. All methods except the static ones must be
// called on that thread.
class LabelManager {
 public:
  static void Configure(Mode mode, winrt::com_ptr<IXamlDiagnostics> diagnostics);
  static LabelManager& ForCurrentThread();
  static bool IsActive();
  // Callable from any thread. Restores every label and stops all managers.
  static void DeactivateAll();
  // Callable from any thread with COM initialized. Re-reads the per-monitor pin settings.
  static void ReloadSettings();

  void OnElementAdded(InstanceHandle handle, const std::wstring& type, const std::wstring& name);
  void OnElementRemoved(InstanceHandle handle);

 private:
  struct TrackedLabel {
    winrt::weak_ref<winrt::Windows::UI::Xaml::Controls::TextBlock> label;
    // The label's parent, whose Width the taskbar pins for icon-only buttons.
    winrt::weak_ref<winrt::Windows::UI::Xaml::FrameworkElement> panel;
    int64_t textToken = 0;
    int64_t visibilityToken = 0;
    int64_t widthToken = 0;
    bool modified = false;
  };

  struct PlacedButton {
    winrt::Windows::UI::Xaml::FrameworkElement button;
    HWND hwnd = nullptr;  // Null for a pinned app that isn't running.
    std::wstring appId;
  };

  LabelManager();
  void EnsureTimer();
  void ScheduleRecompute();
  void Recompute();
  void PlaceButtons();
  HMONITOR RootMonitor(void* root);
  std::wstring AppIdForWindow(HWND hwnd);
  void Show(const PlacedButton& placed, bool show, bool asPin);
  void ShowAll();
  void Apply(TrackedLabel& tracked, winrt::Windows::UI::Xaml::Controls::TextBlock const& label,
             const std::wstring& text);
  void Unregister(TrackedLabel& tracked);
  void RestoreAll();
  void ScheduleDump();
  void Dump();

  winrt::Windows::System::DispatcherQueue queue_{nullptr};
  winrt::Windows::System::DispatcherQueueTimer timer_{nullptr};
  winrt::Windows::System::DispatcherQueueTimer dumpTimer_{nullptr};
  std::unordered_map<InstanceHandle, TrackedLabel> labels_;
  std::unordered_map<InstanceHandle, winrt::weak_ref<winrt::Windows::UI::Xaml::FrameworkElement>>
      buttons_;
  // Width the taskbar gives icon-only buttons, learned from unlabeled buttons.
  double iconOnlyWidth_ = 44;

  int settingsVersion_ = -1;
  PinSettings settings_;
  std::set<std::wstring> pinnedApps_;
  struct CachedAppId {
    std::wstring appId;
    ULONGLONG at = 0;
  };
  std::unordered_map<HWND, CachedAppId> appIds_;
  std::vector<winrt::weak_ref<winrt::Windows::UI::Xaml::Hosting::DesktopWindowXamlSource>> sources_;
  std::unordered_map<void*, HWND> rootWindows_;
  // Keyed by the button's identity, which outlives its InstanceHandle.
  std::unordered_map<void*, winrt::weak_ref<winrt::Windows::UI::Xaml::FrameworkElement>> hidden_;
  // Buttons standing in for a pin whose app only runs on other monitors, and
  // the running indicators hidden on them.
  std::set<void*> standIns_;
  std::unordered_map<void*, winrt::weak_ref<winrt::Windows::UI::Xaml::UIElement>> hiddenIndicators_;
  bool recomputePending_ = false;
  bool writing_ = false;
  bool stopped_ = false;
};

}  // namespace unglom
