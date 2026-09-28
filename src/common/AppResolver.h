/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#pragma once

#include <windows.h>

#include <string>

namespace unglom {

// The taskbar's own app IDs, which is what pinned buttons report ("Appid: <id>")
// and what decides which pin a window belongs to. Lowercased, since app IDs are
// case-insensitive. Empty on failure. Requires COM on the calling thread.
std::wstring TaskbarAppIdForWindow(HWND hwnd);
std::wstring TaskbarAppIdForShortcut(const std::wstring& path);

std::wstring Lowercase(std::wstring s);

}  // namespace unglom
