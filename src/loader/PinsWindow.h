/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#pragma once

#include <windows.h>

#include <functional>

namespace unglom {

// Opens the window for choosing which monitors' taskbars each pinned app
// appears on, or brings it to the front. onChanged runs after every change.
void ShowPinsWindow(HINSTANCE instance, std::function<void()> onChanged);

// For the message loop: keyboard navigation (Tab, Esc) inside the window.
bool IsPinsWindowMessage(MSG* msg);

}  // namespace unglom
