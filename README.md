# Unglom

One taskbar button per window, labels only when you actually need them.

Windows ties two unrelated ideas together: showing a separate button for each
window, and showing window titles. Unglom (named after Windows' own
`TaskbarGlomLevel` setting) pulls them apart on the Windows 11 taskbar:

- An app with a single window gets an icon-only button.
- An app with several windows gets a label on each button, showing only the
  part of the title that differs from its siblings. For example,
  `Inbox - alice@work.com - Outlook` and `Inbox - bob@home.com - Outlook`
  become `alice@work.com` and `bob@home.com`.

## How it works

- `Unglom.exe` is a small tray app that loads `UnglomTap.dll` into the taskbar
  using the XAML diagnostics API (`InitializeXamlDiagnosticsEx`), and re-loads
  it whenever Explorer restarts.
- `UnglomTap.dll` watches the taskbar's XAML visual tree, groups buttons by
  app, and rewrites or hides their labels.
- `TitleDiff` is the pure logic that works out which part of each title is
  distinctive.

## Using it

1. In Windows Settings > Personalization > Taskbar > Taskbar behaviors, set
   "Combine taskbar buttons and hide labels" to **Never**.
2. Double-click `build.cmd`. The first run downloads Microsoft's C++ compiler
   and Windows SDK into `%LOCALAPPDATA%\unglom-toolchain` (no admin needed,
   about 500 MB; this accepts the Visual Studio Build Tools license).
3. Double-click `out\Unglom.exe`. It lives in the notification area (the
   system tray). Its menu has Pause/Resume, Start with Windows, the log
   folder, and Exit. Exiting or pausing puts the taskbar back exactly as it
   was.

Logs are in `%LOCALAPPDATA%\Unglom`.

## Development

- `out\TitleDiffTests.exe` runs the title logic unit tests (`build.cmd` runs
  them automatically).
- `out\Unglom.exe --mode dump` changes nothing and logs the taskbar's XAML
  tree to `%LOCALAPPDATA%\Unglom\tap.log`, which is how to find element names
  when a Windows update changes the taskbar.
- `out\UnglomTestWindows.exe 20 "Inbox - a - Outlook" "Inbox - b - Outlook"`
  opens windows of one app for 20 seconds. A title written `Old=>New` is
  renamed halfway through.
- `tools\capture-taskbar.ps1 <file.png>` screenshots the taskbar.
- `tools\send-menu-command.ps1 <n>` sends a tray menu command (1 = Pause or
  Resume, 4 = Exit).

Explorer keeps every copy of the DLL it has loaded, so each start or resume
loads a freshly named copy from `%LOCALAPPDATA%\Unglom\bin`; older copies go
idle and are cleaned up once Explorer restarts.

## Status

Working on Windows 11 (tested on build 26340).
