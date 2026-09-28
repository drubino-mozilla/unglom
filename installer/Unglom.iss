; This Source Code Form is subject to the terms of the Mozilla Public
; License, v. 2.0. If a copy of the MPL was not distributed with this
; file, You can obtain one at http://mozilla.org/MPL/2.0/.

; Inno Setup script for UnglomSetup-<version>.exe. Build it with
; installer\build-installer.cmd, which builds Unglom first. Installs per user
; (no admin needed) into the same place as install.cmd, so either one can
; update or remove what the other installed.

#define AppName "Unglom"
#define SourceDir "..\out"
#define SourceExe SourceDir + "\Unglom.exe"
; The version comes from src\common\Version.h by way of the built exe.
#define AppVersion GetStringFileInfo(SourceExe, "ProductVersion")
#define RepoUrl "https://github.com/drubino-mozilla/unglom"

[Setup]
AppId={{4646990A-4CB6-492B-A56D-AE7C411ADFF8}
AppName={#AppName}
AppVersion={#AppVersion}
AppVerName={#AppName} {#AppVersion}
AppPublisher=Unglom contributors
AppPublisherURL={#RepoUrl}
AppSupportURL={#RepoUrl}/issues
AppUpdatesURL={#RepoUrl}/releases
AppComments=One taskbar button per window, labels only when you actually need them.
VersionInfoVersion={#AppVersion}
VersionInfoCopyright=Licensed under the Mozilla Public License 2.0
LicenseFile=..\LICENSE

; Per-user install, same location as install.cmd uses.
PrivilegesRequired=lowest
DefaultDirName={localappdata}\Unglom\app
DisableWelcomePage=no
DisableDirPage=yes
DisableProgramGroupPage=yes
DisableReadyPage=yes
UninstallDisplayName={#AppName}
UninstallDisplayIcon={app}\Unglom.exe
UninstallFilesDir={localappdata}\Unglom\uninstall

; Windows 11 (build 22000) or later, 64-bit only.
MinVersion=10.0.22000
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible

; Unglom stops itself in PrepareToInstall; the restart-manager dialog is not needed.
CloseApplications=no
RestartApplications=no

WizardStyle=modern
SetupIconFile=..\res\unglom.ico
OutputDir=..\out
OutputBaseFilename=UnglomSetup-{#AppVersion}
Compression=lzma2/max
SolidCompression=yes

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"

[Messages]
FinishedLabel=Setup has finished installing [name] on your computer. It runs in the notification area (system tray) and starts with Windows.%n%nFor per-window labels to appear, Windows Settings > Personalization > Taskbar > Taskbar behaviors > "Combine taskbar buttons and hide labels" must be set to Never.

[Files]
Source: "{#SourceDir}\Unglom.exe"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#SourceDir}\UnglomTap.dll"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\LICENSE"; DestDir: "{app}"; DestName: "LICENSE.txt"; Flags: ignoreversion

[Icons]
Name: "{userprograms}\{#AppName}"; Filename: "{app}\Unglom.exe"; Comment: "{#AppName}"

[Registry]
; Start with Windows. Unglom's own tray menu toggles this same value.
Root: HKCU; Subkey: "Software\Microsoft\Windows\CurrentVersion\Run"; ValueType: string; ValueName: "Unglom"; ValueData: """{app}\Unglom.exe"""; Flags: uninsdeletevalue

[Run]
Filename: "{app}\Unglom.exe"; Description: "Start {#AppName} now"; Flags: postinstall nowait skipifsilent
Filename: "ms-settings:taskbar"; Description: "Open Windows' taskbar settings (to set ""Combine taskbar buttons"" to Never)"; Flags: postinstall nowait skipifsilent shellexec unchecked; Check: not TaskbarLabelsEnabled

[UninstallRun]
; Stops the running copy and puts the taskbar back before files are removed.
Filename: "{app}\Unglom.exe"; Parameters: "--stop"; Flags: runhidden waituntilterminated; RunOnceId: "StopUnglom"

[UninstallDelete]
; Copies of the DLL that Explorer loaded (see README). Ones still in use stay
; until Explorer restarts. Logs in {localappdata}\Unglom are kept.
Type: filesandordirs; Name: "{localappdata}\Unglom\bin"
Type: dirifempty; Name: "{localappdata}\Unglom\app"

[Code]
// True when "Combine taskbar buttons and hide labels" is set to Never.
function TaskbarLabelsEnabled: Boolean;
var
  Level: Cardinal;
begin
  Result := RegQueryDWordValue(HKEY_CURRENT_USER,
    'Software\Microsoft\Windows\CurrentVersion\Explorer\Advanced', 'TaskbarGlomLevel', Level)
    and (Level = 2);
end;

// Stops any running copy of Unglom (installed, or run from a build directory)
// so its files can be replaced. Uses the new exe, which knows how to find and
// stop the old one and waits for it to exit.
function PrepareToInstall(var NeedsRestart: Boolean): String;
var
  ResultCode: Integer;
begin
  Result := '';
  ExtractTemporaryFile('Unglom.exe');
  Exec(ExpandConstant('{tmp}\Unglom.exe'), '--stop', '', SW_HIDE, ewWaitUntilTerminated, ResultCode);
end;
