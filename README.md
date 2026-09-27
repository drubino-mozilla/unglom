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

It can also put pinned apps on other monitors' taskbars. Windows either shows
every pin (and every window) on every taskbar, or pins on the main taskbar
only. Unglom lets you pick, per pin, which monitors' taskbars it appears on.

## How it works

- `Unglom.exe` is a small tray app that loads `UnglomTap.dll` into the taskbar
  using the XAML diagnostics API (`InitializeXamlDiagnosticsEx`), and re-loads
  it whenever Explorer restarts.
- `UnglomTap.dll` watches the taskbar's XAML visual tree, groups buttons by
  app, and rewrites or hides their labels.
- For pins on other monitors, Windows has to create them, which it only does
  with "Show my taskbar apps on" set to **All taskbars**. While any pin is
  assigned to monitors, Unglom switches that on, remembers your own choice, and
  hides every button your choice wouldn't show plus pins not assigned to that
  monitor. A pinned app running only on other monitors keeps its pin, as
  Windows does.
- `TitleDiff` is the pure logic that works out which part of each title is
  distinctive.

## Using it

1. In Windows Settings > Personalization > Taskbar > Taskbar behaviors, set
   "Combine taskbar buttons and hide labels" to **Never**.
2. Double-click `install.cmd`. It builds Unglom, copies it to
   `%LOCALAPPDATA%\Unglom\app`, makes it start with Windows, adds it to the
   Start menu, and starts it. The first build downloads Microsoft's C++
   compiler and Windows SDK into `%LOCALAPPDATA%\unglom-toolchain` (no admin
   needed, about 500 MB; this accepts the Visual Studio Build Tools license).
3. Unglom lives in the notification area (the system tray). Its menu has
   Pause/Resume, Start with Windows, the log folder, and Exit. Exiting or
   pausing puts the taskbar back exactly as it was.
4. To put a pin on other monitors, pin the app as usual, then choose
   **Pinned apps on each monitor...** in Unglom's menu. The window lists every
   pinned app with a checkbox per monitor; tick the monitors each app should
   appear on. Changes apply right away. **Put every pin back where Windows
   puts it** undoes all of that.

While any pin is assigned, Windows Settings shows "Show my taskbar apps on" as
"All taskbars". Picking a different option there still works: Unglom takes it
as your new choice.

Run `install.cmd` again to update the installed copy; `uninstall.cmd` removes
it. Logs are in `%LOCALAPPDATA%\Unglom`.

## Development

`build.cmd` builds into `out\` without touching the installed copy. Running
`out\Unglom.exe` replaces whichever copy is running (only one runs at a time);
start the installed one again from the Start menu when done.

- `out\TitleDiffTests.exe` runs the title logic unit tests (`build.cmd` runs
  them automatically).
- `out\Unglom.exe --mode dump` changes nothing and logs the taskbar's XAML
  tree to `%LOCALAPPDATA%\Unglom\tap.log`, which is how to find element names
  when a Windows update changes the taskbar.
- `out\UnglomTestWindows.exe 20 "Inbox - a - Outlook" "Inbox - b - Outlook"`
  opens windows of one app for 20 seconds. A title written `Old=>New` is
  renamed halfway through. `--appid <id>` makes them belong to another app,
  such as a pinned one, and `--at <x>,<y>` places them, e.g. on another monitor.
- `tools\list-taskbar.ps1` lists every taskbar's buttons.
- `tools\capture-taskbar.ps1 <file.png> [-All]` screenshots the main taskbar,
  or every taskbar.
- `tools\send-menu-command.ps1 <n>` sends a tray menu command (1 = Pause or
  Resume, 4 = Exit, 5 = the pinned apps window).

Explorer keeps every copy of the DLL it has loaded, so each start or resume
loads a freshly named copy from `%LOCALAPPDATA%\Unglom\bin`; older copies go
idle and are cleaned up once Explorer restarts.

## Status

Working on Windows 11 (tested on build 26340).
