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
And it can align each monitor's taskbar icons left, center or right, where
Windows only offers left or center for all taskbars at once.

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

## Installing

1. Download `UnglomSetup-<version>.exe` from the
   [latest release](https://github.com/drubino-mozilla/unglom/releases/latest)
   and run it. It installs for the current user only (no admin rights needed)
   into `%LOCALAPPDATA%\Unglom\app`, makes Unglom start with Windows, adds it
   to the Start menu, and starts it.

   The installer isn't code-signed, so Windows SmartScreen may show
   "Windows protected your PC" the first time. Click **More info**, then
   **Run anyway**. Each release also has a `.sha256` file to check the
   download against.
2. In Windows Settings > Personalization > Taskbar > Taskbar behaviors, set
   "Combine taskbar buttons and hide labels" to **Never**. The installer
   offers to open that page if it isn't set already.

To update, run the newer installer over the old one. To remove it, use
Windows Settings > Apps > Installed apps, like any other program. Logs in
`%LOCALAPPDATA%\Unglom` are kept.

Unglom needs Windows 11 (64-bit).

## Using it

1. Unglom lives in the notification area (the system tray). Its menu has
   Pause/Resume, Start with Windows, the log folder, and Exit. Exiting or
   pausing puts the taskbar back exactly as it was.
2. To put a pin on other monitors, pin the app as usual, then choose
   **Pinned apps on each monitor...** in Unglom's menu. The window lists every
   pinned app with a checkbox per monitor; tick the monitors each app should
   appear on. Changes apply right away. **Put every pin back where Windows
   puts it** undoes all of that.
3. The same window has an **Icon alignment** dropdown above each monitor's
   column. The icons (and the Start button, which sits with them) move to
   that side of the taskbar; on the right they stop short of the clock.

While any pin is assigned, Windows Settings shows "Show my taskbar apps on" as
"All taskbars". Picking a different option there still works: Unglom takes it
as your new choice.

## Building from source

Double-click `install.cmd`. It builds Unglom and installs it exactly as the
installer does (same folder, Start menu entry and startup setting, so the two
can update or remove each other's work). The first build downloads Microsoft's
C++ compiler and Windows SDK into `%LOCALAPPDATA%\unglom-toolchain` (no admin
needed, about 500 MB; this accepts the Visual Studio Build Tools license).
Run `install.cmd` again to update the installed copy; `uninstall.cmd` removes
it.

`installer\build-installer.cmd` builds `out\UnglomSetup-<version>.exe`, the
same installer the releases ship. On first use it downloads
[Inno Setup](https://jrsoftware.org/isinfo.php) into the same toolchain
folder (this accepts its license).

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

## Releasing

The version number lives in `src\common\Version.h` and is stamped into
`Unglom.exe`, `UnglomTap.dll` and the installer. To publish a release, bump
it, commit, tag the commit `v<major>.<minor>.<patch>` and push the tag. The
[Build workflow](.github/workflows/build.yml) checks the tag against
`Version.h`, builds the installer and attaches it (with a `.sha256` file) to a
new GitHub release.

## Status

Working on Windows 11 (tested on build 26340.9596).

## License

Unglom is released under the
[Mozilla Public License 2.0](https://mozilla.org/MPL/2.0/); see
[LICENSE](LICENSE).
