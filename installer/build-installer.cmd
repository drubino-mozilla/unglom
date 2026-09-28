@echo off
rem This Source Code Form is subject to the terms of the Mozilla Public
rem License, v. 2.0. If a copy of the MPL was not distributed with this
rem file, You can obtain one at http://mozilla.org/MPL/2.0/.

rem Builds Unglom and then out\UnglomSetup-<version>.exe, the installer that
rem the GitHub release ships. Downloads Inno Setup on first use (per user, no
rem admin needed) unless ISCC.exe is already on the PATH or in its usual place.
rem Double-click it, or run "build-installer.cmd nopause" from scripts.
setlocal
set "ROOT=%~dp0.."
set "ISCC="

call "%ROOT%\build.cmd" nopause || goto :fail

for %%p in (ISCC.exe) do if not "%%~$PATH:p"=="" set "ISCC=%%~$PATH:p"
if not defined ISCC if exist "%LOCALAPPDATA%\unglom-toolchain\innosetup\ISCC.exe" set "ISCC=%LOCALAPPDATA%\unglom-toolchain\innosetup\ISCC.exe"
if not defined ISCC if exist "%ProgramFiles(x86)%\Inno Setup 6\ISCC.exe" set "ISCC=%ProgramFiles(x86)%\Inno Setup 6\ISCC.exe"
if not defined ISCC if exist "%LOCALAPPDATA%\Programs\Inno Setup 6\ISCC.exe" set "ISCC=%LOCALAPPDATA%\Programs\Inno Setup 6\ISCC.exe"
if not defined ISCC (
  echo Inno Setup not found. Downloading it now...
  call "%ROOT%\tools\setup-innosetup.cmd" || goto :fail
  set "ISCC=%LOCALAPPDATA%\unglom-toolchain\innosetup\ISCC.exe"
)

echo.
echo === Installer ===
del /q "%ROOT%\out\UnglomSetup-*.exe" 2>nul
"%ISCC%" /Q "%~dp0Unglom.iss" || goto :fail
for %%f in ("%ROOT%\out\UnglomSetup-*.exe") do echo Built %%~ff

echo.
echo Installer build succeeded.
if /i not "%~1"=="nopause" pause
exit /b 0

:fail
echo.
echo INSTALLER BUILD FAILED
if /i not "%~1"=="nopause" pause
exit /b 1
