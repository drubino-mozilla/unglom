/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

// COM entry points loaded into explorer.exe by InitializeXamlDiagnosticsEx.

#include "TapCommon.h"

#include <thread>
#include <vector>

#include "../common/Log.h"
#include "../common/PinSettings.h"
#include "LabelManager.h"

using namespace unglom;

// {5a5848b4-a64b-4360-978b-a402c2902714}
static constexpr CLSID CLSID_UnglomTap = {
    0x5a5848b4, 0xa64b, 0x4360, {0x97, 0x8b, 0xa4, 0x02, 0xc2, 0x90, 0x27, 0x14}};

namespace {

struct InitData {
  DWORD loaderPid = 0;
  std::wstring stopEvent;
  Mode mode = Mode::Run;
};

// Format: "pid=1234;stop=Local\Name;mode=run"
InitData ParseInitData(const std::wstring& data) {
  InitData result;
  size_t pos = 0;
  while (pos <= data.size()) {
    size_t end = data.find(L';', pos);
    if (end == std::wstring::npos) end = data.size();
    std::wstring item = data.substr(pos, end - pos);
    size_t eq = item.find(L'=');
    if (eq != std::wstring::npos) {
      std::wstring key = item.substr(0, eq);
      std::wstring value = item.substr(eq + 1);
      if (key == L"pid") result.loaderPid = wcstoul(value.c_str(), nullptr, 10);
      if (key == L"stop") result.stopEvent = value;
      if (key == L"mode") result.mode = value == L"dump" ? Mode::Dump : Mode::Run;
    }
    pos = end + 1;
  }
  return result;
}

class VisualTreeWatcher
    : public winrt::implements<VisualTreeWatcher, IVisualTreeServiceCallback2, winrt::non_agile> {
 public:
  explicit VisualTreeWatcher(Mode mode) : mode_(mode) {}

  HRESULT STDMETHODCALLTYPE OnVisualTreeChange(ParentChildRelation relation, VisualElement element,
                                               VisualMutationType mutation) noexcept override {
    if (!LabelManager::IsActive()) return S_OK;
    try {
      std::wstring type = element.Type ? element.Type : L"";
      std::wstring name = element.Name ? element.Name : L"";
      if (mode_ == Mode::Dump) {
        Log(L"%lc %ls#%ls h=%llx parent=%llx index=%u", mutation == Add ? L'+' : L'-',
            type.c_str(), name.c_str(), element.Handle, relation.Parent, relation.ChildIndex);
      }
      LabelManager& manager = LabelManager::ForCurrentThread();
      if (mutation == Add) {
        manager.OnElementAdded(element.Handle, type, name);
      } else {
        manager.OnElementRemoved(element.Handle);
      }
    } catch (winrt::hresult_error const& e) {
      Log(L"OnVisualTreeChange failed: 0x%08X %ls", static_cast<unsigned>(e.code().value),
          e.message().c_str());
    } catch (...) {
      Log(L"OnVisualTreeChange failed");
    }
    return S_OK;
  }

  HRESULT STDMETHODCALLTYPE OnElementStateChanged(InstanceHandle, VisualElementState,
                                                  LPCWSTR) noexcept override {
    return S_OK;
  }

 private:
  Mode mode_;
};

class Tap : public winrt::implements<Tap, IObjectWithSite, winrt::non_agile> {
 public:
  HRESULT STDMETHODCALLTYPE SetSite(IUnknown* site) noexcept override {
    diagnostics_ = nullptr;
    if (!site) return S_OK;
    try {
      winrt::check_hresult(site->QueryInterface(IID_PPV_ARGS(diagnostics_.put())));
      BSTR raw = nullptr;
      diagnostics_->GetInitializationData(&raw);
      std::wstring data = raw ? raw : L"";
      SysFreeString(raw);
      init_ = ParseInitData(data);
      Log(L"SetSite, init data \"%ls\"", data.c_str());

      LabelManager::Configure(init_.mode, diagnostics_);
      watcher_ = winrt::make_self<VisualTreeWatcher>(init_.mode);
      // AdviseVisualTreeChange deadlocks if called on the UI thread.
      std::thread([self = get_strong()] { self->Run(); }).detach();
    } catch (...) {
      return winrt::to_hresult();
    }
    return S_OK;
  }

  HRESULT STDMETHODCALLTYPE GetSite(REFIID riid, void** out) noexcept override {
    *out = nullptr;
    return diagnostics_ ? diagnostics_->QueryInterface(riid, out) : E_FAIL;
  }

 private:
  void Run() {
    auto service = diagnostics_.try_as<IVisualTreeService3>();
    HRESULT hr = service ? service->AdviseVisualTreeChange(watcher_.get()) : E_NOINTERFACE;
    Log(L"AdviseVisualTreeChange returned 0x%08X", static_cast<unsigned>(hr));
    if (FAILED(hr)) return;

    HANDLE stops[2];
    DWORD stopCount = 0;
    HANDLE stopEvent = nullptr;
    if (HANDLE process = OpenProcess(SYNCHRONIZE, FALSE, init_.loaderPid)) stops[stopCount++] = process;
    if (!init_.stopEvent.empty()) {
      stopEvent = OpenEventW(SYNCHRONIZE, FALSE, init_.stopEvent.c_str());
      if (stopEvent) stops[stopCount++] = stopEvent;
    }
    if (stopCount > 0) {
      if (init_.mode == Mode::Run) {
        WatchSettings(stops, stopCount);
      } else {
        WaitForMultipleObjects(stopCount, stops, FALSE, INFINITE);
      }
    }
    bool stopRequested = stopEvent && WaitForSingleObject(stopEvent, 0) == WAIT_OBJECT_0;
    for (DWORD i = 0; i < stopCount; ++i) CloseHandle(stops[i]);

    Log(L"Loader stopped or exited; deactivating");
    LabelManager::DeactivateAll();
    service->UnadviseVisualTreeChange(watcher_.get());

    // A loader that exits normally signals first and puts the setting back itself.
    TaskbarApps own;
    if (!stopRequested && ReadTakenOver(&own)) {
      Log(L"Loader exited without stopping; putting back \"Show my taskbar apps on\"");
      WriteTaskbarAppsSetting(own);
      ClearTakenOver();
    }
  }

  // Reloads the pin settings whenever they or the taskbar's pinned shortcuts
  // change, until one of the stop handles is signaled.
  void WatchSettings(HANDLE* stops, DWORD stopCount) {
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    const wchar_t* keyPaths[] = {L"Software\\Unglom", L"Software\\Unglom\\PinMonitors",
                                 L"Software\\Unglom\\IconAlignment", kTaskbandKey};
    constexpr int kKeyCount = ARRAYSIZE(keyPaths);
    HKEY keys[kKeyCount] = {};
    HANDLE keyEvents[kKeyCount] = {};
    auto watchKey = [&](int i) {
      if (keys[i]) RegNotifyChangeKeyValue(keys[i], FALSE, REG_NOTIFY_CHANGE_LAST_SET, keyEvents[i], TRUE);
    };
    std::vector<HANDLE> waits(stops, stops + stopCount);
    for (int i = 0; i < kKeyCount; ++i) {
      RegCreateKeyExW(HKEY_CURRENT_USER, keyPaths[i], 0, nullptr, 0, KEY_NOTIFY, nullptr, &keys[i],
                      nullptr);
      keyEvents[i] = CreateEventW(nullptr, FALSE, FALSE, nullptr);
      watchKey(i);
      waits.push_back(keyEvents[i]);
    }
    HANDLE shortcuts = FindFirstChangeNotificationW(
        PinnedShortcutsDir().c_str(), FALSE, FILE_NOTIFY_CHANGE_FILE_NAME | FILE_NOTIFY_CHANGE_LAST_WRITE);
    if (shortcuts != INVALID_HANDLE_VALUE) waits.push_back(shortcuts);

    LabelManager::ReloadSettings();
    for (;;) {
      DWORD result = WaitForMultipleObjects(static_cast<DWORD>(waits.size()), waits.data(), FALSE, INFINITE);
      DWORD index = result - WAIT_OBJECT_0;
      if (index < stopCount || index >= waits.size()) break;
      Sleep(200);  // Settle a burst of changes into one reload.
      for (int i = 0; i < kKeyCount; ++i) {
        if (waits[index] == keyEvents[i]) watchKey(i);
      }
      if (waits[index] == shortcuts) FindNextChangeNotification(shortcuts);
      LabelManager::ReloadSettings();
    }

    if (shortcuts != INVALID_HANDLE_VALUE) FindCloseChangeNotification(shortcuts);
    for (int i = 0; i < kKeyCount; ++i) {
      if (keys[i]) RegCloseKey(keys[i]);
      CloseHandle(keyEvents[i]);
    }
    CoUninitialize();
  }

  winrt::com_ptr<IXamlDiagnostics> diagnostics_;
  winrt::com_ptr<VisualTreeWatcher> watcher_;
  InitData init_;
};

class Factory : public winrt::implements<Factory, IClassFactory> {
 public:
  HRESULT STDMETHODCALLTYPE CreateInstance(IUnknown* outer, REFIID riid, void** out) noexcept override {
    *out = nullptr;
    if (outer) return CLASS_E_NOAGGREGATION;
    try {
      return winrt::make_self<Tap>()->QueryInterface(riid, out);
    } catch (...) {
      return winrt::to_hresult();
    }
  }

  HRESULT STDMETHODCALLTYPE LockServer(BOOL) noexcept override { return S_OK; }
};

}  // namespace

STDAPI DllGetClassObject(REFCLSID clsid, REFIID riid, void** out) {
  if (!out) return E_POINTER;
  *out = nullptr;
  if (clsid != CLSID_UnglomTap) return CLASS_E_CLASSNOTAVAILABLE;
  LogInit(L"tap.log", L"tap");
  try {
    return winrt::make_self<Factory>()->QueryInterface(riid, out);
  } catch (...) {
    return winrt::to_hresult();
  }
}

// Visual tree callbacks can arrive at any time, so never unload.
STDAPI DllCanUnloadNow() { return S_FALSE; }
