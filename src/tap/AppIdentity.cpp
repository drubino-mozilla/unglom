/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "AppIdentity.h"

#include <objbase.h>
#include <appmodel.h>
#include <propsys.h>
#include <shellapi.h>
#include <shobjidl.h>
#include <propkey.h>

#include <algorithm>
#include <cwctype>

namespace unglom {
namespace {

std::wstring WindowAumid(HWND hwnd) {
  IPropertyStore* store = nullptr;
  if (FAILED(SHGetPropertyStoreForWindow(hwnd, IID_PPV_ARGS(&store)))) return {};
  std::wstring result;
  PROPVARIANT value;
  PropVariantInit(&value);
  if (SUCCEEDED(store->GetValue(PKEY_AppUserModel_ID, &value)) && value.vt == VT_LPWSTR &&
      value.pwszVal) {
    result = value.pwszVal;
  }
  PropVariantClear(&value);
  store->Release();
  return result;
}

std::wstring ProcessKey(HWND hwnd) {
  DWORD pid = 0;
  GetWindowThreadProcessId(hwnd, &pid);
  HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
  if (!process) return L"pid:" + std::to_wstring(pid);

  std::wstring result;
  wchar_t buffer[APPLICATION_USER_MODEL_ID_MAX_LENGTH];
  UINT32 length = ARRAYSIZE(buffer);
  if (GetApplicationUserModelId(process, &length, buffer) == ERROR_SUCCESS) {
    result = std::wstring(L"aumid:") + buffer;
  } else {
    wchar_t path[MAX_PATH * 2];
    DWORD size = ARRAYSIZE(path);
    if (QueryFullProcessImageNameW(process, 0, path, &size)) {
      result = std::wstring(L"exe:") + path;
      std::transform(result.begin(), result.end(), result.begin(), ::towlower);
    } else {
      result = L"pid:" + std::to_wstring(pid);
    }
  }
  CloseHandle(process);
  return result;
}

}  // namespace

std::wstring AppKeyForWindow(HWND hwnd) {
  std::wstring aumid = WindowAumid(hwnd);
  if (!aumid.empty()) return L"aumid:" + aumid;
  return ProcessKey(hwnd);
}

std::wstring WindowTitle(HWND hwnd) {
  wchar_t buffer[1024];
  int n = InternalGetWindowText(hwnd, buffer, ARRAYSIZE(buffer));
  return std::wstring(buffer, n > 0 ? n : 0);
}

}  // namespace unglom
