@echo off
rem This Source Code Form is subject to the terms of the Mozilla Public
rem License, v. 2.0. If a copy of the MPL was not distributed with this
rem file, You can obtain one at http://mozilla.org/MPL/2.0/.

rem Stops Unglom and removes the installed copy, the Start with Windows entry
rem and the Start menu shortcut. Logs in %LOCALAPPDATA%\Unglom are kept.
rem Double-click it, or run "uninstall.cmd nopause" from scripts.
setlocal
set "APP=%LOCALAPPDATA%\Unglom\app"
set "UNINS=%LOCALAPPDATA%\Unglom\uninstall\unins000.exe"

if exist "%APP%\Unglom.exe" "%APP%\Unglom.exe" --stop
rem If UnglomSetup installed it, let its uninstaller remove its Apps entry too.
if exist "%UNINS%" "%UNINS%" /VERYSILENT /SUPPRESSMSGBOXES /NORESTART
reg delete "HKCU\Software\Microsoft\Windows\CurrentVersion\Run" /v Unglom /f >nul 2>&1
del "%APPDATA%\Microsoft\Windows\Start Menu\Programs\Unglom.lnk" >nul 2>&1
rmdir /s /q "%APP%" 2>nul

echo Unglom is uninstalled.
if /i not "%~1"=="nopause" pause
exit /b 0
