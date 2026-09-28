@echo off
rem This Source Code Form is subject to the terms of the Mozilla Public
rem License, v. 2.0. If a copy of the MPL was not distributed with this
rem file, You can obtain one at http://mozilla.org/MPL/2.0/.

rem Downloads Microsoft's C++ compiler and Windows SDK into
rem %LOCALAPPDATA%\unglom-toolchain without needing admin rights, using
rem https://gist.github.com/mmozeiko/7f3162ec2988e81e56d5c4e22cde9977
rem Running this accepts the Visual Studio Build Tools license.
setlocal
set "DEST=%LOCALAPPDATA%\unglom-toolchain"
if not exist "%DEST%" mkdir "%DEST%"
cd /d "%DEST%"
powershell -NoProfile -Command "Invoke-WebRequest -UseBasicParsing https://gist.githubusercontent.com/mmozeiko/7f3162ec2988e81e56d5c4e22cde9977/raw/portable-msvc.py -OutFile portable-msvc.py" || exit /b 1
python portable-msvc.py --accept-license --target x64 || exit /b 1
