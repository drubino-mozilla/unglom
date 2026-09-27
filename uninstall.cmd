@echo off
rem Stops Unglom and removes the installed copy, the Start with Windows entry
rem and the Start menu shortcut. Logs in %LOCALAPPDATA%\Unglom are kept.
rem Double-click it, or run "uninstall.cmd nopause" from scripts.
setlocal
set "APP=%LOCALAPPDATA%\Unglom\app"

if exist "%APP%\Unglom.exe" "%APP%\Unglom.exe" --stop
reg delete "HKCU\Software\Microsoft\Windows\CurrentVersion\Run" /v Unglom /f >nul 2>&1
del "%APPDATA%\Microsoft\Windows\Start Menu\Programs\Unglom.lnk" >nul 2>&1
rmdir /s /q "%APP%" 2>nul

echo Unglom is uninstalled.
if /i not "%~1"=="nopause" pause
exit /b 0
