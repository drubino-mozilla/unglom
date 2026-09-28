/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#pragma once

#include <string>
#include <vector>

namespace unglom {

// Given the titles of all windows that share one taskbar icon, returns a
// short label for each: the part of its title that is not shared by every
// title in the group. Titles are compared first by separator-delimited
// segments (" - ", " | ", ...), then by whole words.
std::vector<std::wstring> DistinctLabels(const std::vector<std::wstring>& titles);

}  // namespace unglom
