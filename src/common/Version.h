/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

// The one place Unglom's version number lives. Both the .rc files and the
// installer (which reads it back out of the built Unglom.exe) use it.
// Bump it, tag the commit v<major>.<minor>.<patch>, and push the tag to
// publish a release.
#pragma once

#define UNGLOM_VERSION_MAJOR 1
#define UNGLOM_VERSION_MINOR 0
#define UNGLOM_VERSION_PATCH 1

#define UNGLOM_STR2(x) #x
#define UNGLOM_STR(x) UNGLOM_STR2(x)
#define UNGLOM_VERSION_STRING \
  UNGLOM_STR(UNGLOM_VERSION_MAJOR) "." UNGLOM_STR(UNGLOM_VERSION_MINOR) "." UNGLOM_STR(UNGLOM_VERSION_PATCH)
