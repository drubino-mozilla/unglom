/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#pragma once

#include <windows.h>

#include <string>

namespace unglom {

// A key that is equal for windows the taskbar gives the same icon: the
// window's explicit AppUserModelID if it has one, else the packaged app's ID,
// else the executable path.
std::wstring AppKeyForWindow(HWND hwnd);

// The window title, read without sending messages to the window.
std::wstring WindowTitle(HWND hwnd);

}  // namespace unglom
