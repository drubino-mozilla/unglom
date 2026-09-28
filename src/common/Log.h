/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#pragma once

#include <string>

namespace unglom {

// %LOCALAPPDATA%\Unglom, created on first use.
std::wstring DataDir();

// Opens (appending) %LOCALAPPDATA%\Unglom\<fileName>. Until called, Log() is a no-op.
void LogInit(const wchar_t* fileName, const wchar_t* tag);

void Log(_Printf_format_string_ const wchar_t* format, ...);

}  // namespace unglom
