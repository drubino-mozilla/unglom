/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "Log.h"

#include <windows.h>
#include <shlobj.h>

#include <cstdarg>
#include <cstdio>
#include <mutex>

namespace unglom {
namespace {

std::mutex g_mutex;
FILE* g_file = nullptr;
std::wstring g_tag;

}  // namespace

std::wstring DataDir() {
  wchar_t base[MAX_PATH] = {};
  if (!GetEnvironmentVariableW(L"LOCALAPPDATA", base, MAX_PATH)) return L".";
  std::wstring dir = std::wstring(base) + L"\\Unglom";
  CreateDirectoryW(dir.c_str(), nullptr);
  return dir;
}

void LogInit(const wchar_t* fileName, const wchar_t* tag) {
  std::lock_guard lock(g_mutex);
  if (g_file) return;
  std::wstring path = DataDir() + L"\\" + fileName;
  g_file = _wfsopen(path.c_str(), L"a, ccs=UTF-8", _SH_DENYNO);
  g_tag = tag;
}

void Log(const wchar_t* format, ...) {
  std::lock_guard lock(g_mutex);
  if (!g_file) return;
  SYSTEMTIME t;
  GetLocalTime(&t);
  fwprintf(g_file, L"%02d:%02d:%02d.%03d [%ls:%lu] ", t.wHour, t.wMinute, t.wSecond,
           t.wMilliseconds, g_tag.c_str(), GetCurrentThreadId());
  va_list args;
  va_start(args, format);
  vfwprintf(g_file, format, args);
  va_end(args);
  fputwc(L'\n', g_file);
  fflush(g_file);
}

}  // namespace unglom
