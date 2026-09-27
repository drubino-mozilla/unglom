@echo off
rem Builds Unglom, installs it into %LOCALAPPDATA%\Unglom\app, makes it start
rem with Windows, adds a Start menu shortcut, and starts it. Later rebuilds
rem don't touch the installed copy; run this again to update it.
rem Double-click it, or run "install.cmd nopause" from scripts.
setlocal
set "ROOT=%~dp0"
set "APP=%LOCALAPPDATA%\Unglom\app"
set "SHORTCUT=%APPDATA%\Microsoft\Windows\Start Menu\Programs\Unglom.lnk"

call "%ROOT%build.cmd" nopause || goto :fail

echo.
echo === Installing to %APP% ===
if exist "%APP%\Unglom.exe" "%APP%\Unglom.exe" --stop-this-copy
if not exist "%APP%" mkdir "%APP%"
copy /y "%ROOT%out\Unglom.exe" "%APP%\" >nul || goto :fail
copy /y "%ROOT%out\UnglomTap.dll" "%APP%\" >nul || goto :fail
reg add "HKCU\Software\Microsoft\Windows\CurrentVersion\Run" /v Unglom /t REG_SZ ^
  /d "\"%APP%\Unglom.exe\"" /f >nul || goto :fail
powershell -NoProfile -Command ^
  "$s = (New-Object -ComObject WScript.Shell).CreateShortcut($env:SHORTCUT);" ^
  "$s.TargetPath = \"$env:APP\Unglom.exe\"; $s.Description = 'Unglom'; $s.Save()" || goto :fail
start "" "%APP%\Unglom.exe"

echo.
echo Unglom is installed and running. It starts with Windows and is in the Start menu.
if /i not "%~1"=="nopause" pause
exit /b 0

:fail
echo.
echo INSTALL FAILED
if /i not "%~1"=="nopause" pause
exit /b 1
