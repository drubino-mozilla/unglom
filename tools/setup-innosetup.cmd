@echo off
rem This Source Code Form is subject to the terms of the Mozilla Public
rem License, v. 2.0. If a copy of the MPL was not distributed with this
rem file, You can obtain one at http://mozilla.org/MPL/2.0/.

rem Installs Inno Setup (which builds the UnglomSetup installer) for the
rem current user into %LOCALAPPDATA%\unglom-toolchain\innosetup, without
rem needing admin rights. Running this accepts the Inno Setup license
rem (https://jrsoftware.org/files/is/license.txt).
setlocal
set "VERSION=6.7.3"
set "URL=https://github.com/jrsoftware/issrc/releases/download/is-%VERSION:.=_%/innosetup-%VERSION%.exe"
set "DEST=%LOCALAPPDATA%\unglom-toolchain\innosetup"
set "SETUP=%TEMP%\innosetup-%VERSION%.exe"
if not exist "%LOCALAPPDATA%\unglom-toolchain" mkdir "%LOCALAPPDATA%\unglom-toolchain"
echo Downloading Inno Setup %VERSION%...
powershell -NoProfile -Command "Invoke-WebRequest -UseBasicParsing '%URL%' -OutFile '%SETUP%'" || exit /b 1
echo Installing Inno Setup into %DEST%...
"%SETUP%" /VERYSILENT /SUPPRESSMSGBOXES /NORESTART /CURRENTUSER /NOICONS /DIR="%DEST%" /TASKS="" /MERGETASKS="" || exit /b 1
del "%SETUP%" >nul 2>&1
if not exist "%DEST%\ISCC.exe" (
  echo Inno Setup did not install.
  exit /b 1
)
exit /b 0
