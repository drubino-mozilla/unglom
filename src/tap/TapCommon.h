#pragma once

#include <windows.h>
#include <unknwn.h>
#include <ocidl.h>
#include <xamlom.h>

// Clashes with a WinRT method of the same name.
#undef GetCurrentTime

#include <winrt/base.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.System.h>
#include <winrt/Windows.UI.Xaml.h>
#include <winrt/Windows.UI.Xaml.Automation.Peers.h>
#include <winrt/Windows.UI.Xaml.Controls.h>
#include <winrt/Windows.UI.Xaml.Hosting.h>
#include <winrt/Windows.UI.Xaml.Media.h>

#include <windows.ui.xaml.hosting.desktopwindowxamlsource.h>
