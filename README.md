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

## Status

Early development. Targets Windows 11 with "Combine taskbar buttons" set to
"Never".
