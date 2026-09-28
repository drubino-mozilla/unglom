/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "AppResolver.h"

#include <objbase.h>
#include <shlobj.h>
#include <shobjidl.h>

#include <algorithm>
#include <cwctype>

namespace unglom {
namespace {

// Undocumented, but unchanged since Windows 8 and what the taskbar itself uses.
constexpr CLSID CLSID_StartMenuCacheAndAppResolver = {
    0x660b90c8, 0x73a9, 0x4b58, {0x8c, 0xae, 0x35, 0x5b, 0x7f, 0x55, 0x34, 0x1b}};

MIDL_INTERFACE("de25675a-72de-44b4-9373-05170450c140")
IApplicationResolver : public IUnknown {
  virtual HRESULT STDMETHODCALLTYPE GetAppIDForShortcut(IShellItem * item, LPWSTR * appId) = 0;
  virtual HRESULT STDMETHODCALLTYPE GetAppIDForShortcutObject(IShellLinkW * link, IShellItem * item,
                                                              LPWSTR * appId) = 0;
  virtual HRESULT STDMETHODCALLTYPE GetAppIDForWindow(HWND hwnd, LPWSTR * appId,
                                                      BOOL * pinningPrevented, BOOL * explicitAppId,
                                                      BOOL * embeddedShortcutValid) = 0;
};

IApplicationResolver* Resolver() {
  thread_local IApplicationResolver* resolver = nullptr;
  if (!resolver) {
    CoCreateInstance(CLSID_StartMenuCacheAndAppResolver, nullptr, CLSCTX_INPROC_SERVER,
                     __uuidof(IApplicationResolver), reinterpret_cast<void**>(&resolver));
  }
  return resolver;
}

std::wstring TakeString(LPWSTR s) {
  std::wstring result = s ? Lowercase(s) : std::wstring();
  CoTaskMemFree(s);
  return result;
}

}  // namespace

std::wstring Lowercase(std::wstring s) {
  std::transform(s.begin(), s.end(), s.begin(), ::towlower);
  return s;
}

std::wstring TaskbarAppIdForWindow(HWND hwnd) {
  IApplicationResolver* resolver = Resolver();
  if (!resolver) return {};
  LPWSTR appId = nullptr;
  BOOL pinningPrevented = FALSE, explicitAppId = FALSE, embeddedShortcutValid = FALSE;
  if (FAILED(resolver->GetAppIDForWindow(hwnd, &appId, &pinningPrevented, &explicitAppId,
                                         &embeddedShortcutValid))) {
    return {};
  }
  return TakeString(appId);
}

std::wstring TaskbarAppIdForShortcut(const std::wstring& path) {
  IApplicationResolver* resolver = Resolver();
  if (!resolver) return {};
  IShellItem* item = nullptr;
  if (FAILED(SHCreateItemFromParsingName(path.c_str(), nullptr, IID_PPV_ARGS(&item)))) return {};
  LPWSTR appId = nullptr;
  HRESULT hr = resolver->GetAppIDForShortcut(item, &appId);
  item->Release();
  return SUCCEEDED(hr) ? TakeString(appId) : std::wstring();
}

}  // namespace unglom
