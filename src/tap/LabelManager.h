#pragma once

#include "TapCommon.h"

#include <string>
#include <unordered_map>

namespace unglom {

enum class Mode {
  Dump,  // Only log the taskbar's visual tree; change nothing.
  Run,
};

// Owns the taskbar label rewriting for one XAML UI thread. All methods except
// the static ones must be called on that thread.
class LabelManager {
 public:
  static void Configure(Mode mode, winrt::com_ptr<IXamlDiagnostics> diagnostics);
  static LabelManager& ForCurrentThread();
  static bool IsActive();
  // Callable from any thread. Restores every label and stops all managers.
  static void DeactivateAll();

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

  LabelManager();
  void ScheduleRecompute();
  void Recompute();
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
  bool recomputePending_ = false;
  bool writing_ = false;
  bool stopped_ = false;
};

}  // namespace unglom
