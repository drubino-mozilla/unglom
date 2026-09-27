@echo off
rem Builds everything into out\ and runs the unit tests.
rem Double-click it, or run "build.cmd nopause" from scripts.
setlocal
set "ROOT=%~dp0"
set "TOOLCHAIN=%LOCALAPPDATA%\unglom-toolchain\msvc"

if not exist "%TOOLCHAIN%\setup_x64.bat" (
  echo Compiler not found. Downloading it now, this takes a few minutes...
  call "%ROOT%tools\setup-toolchain.cmd" || goto :fail
)
call "%TOOLCHAIN%\setup_x64.bat"

set "OUT=%ROOT%out"
for %%d in (tests tap loader) do if not exist "%OUT%\obj\%%d" mkdir "%OUT%\obj\%%d"

set CFLAGS=/nologo /std:c++20 /EHsc /W4 /O2 /MT /utf-8 /permissive- /bigobj /Zi ^
  /DUNICODE /D_UNICODE /DWIN32_LEAN_AND_MEAN /DNOMINMAX

echo.
echo === Unit tests ===
cl %CFLAGS% /Fo"%OUT%\obj\tests\\" /Fd"%OUT%\obj\tests\\" /Fe"%OUT%\TitleDiffTests.exe" ^
  "%ROOT%tests\TitleDiffTests.cpp" "%ROOT%src\common\TitleDiff.cpp" || goto :fail
cl %CFLAGS% /Fo"%OUT%\obj\tests\\" /Fd"%OUT%\obj\tests\\" /Fe"%OUT%\UnglomTestWindows.exe" ^
  "%ROOT%tests\TestWindows.cpp" /link /SUBSYSTEM:WINDOWS user32.lib shell32.lib || goto :fail

echo.
echo === UnglomTap.dll ===
cl %CFLAGS% /LD /Fo"%OUT%\obj\tap\\" /Fd"%OUT%\obj\tap\\" /Fe"%OUT%\UnglomTap.dll" ^
  "%ROOT%src\tap\UnglomTap.cpp" "%ROOT%src\tap\LabelManager.cpp" "%ROOT%src\tap\AppIdentity.cpp" ^
  "%ROOT%src\common\TitleDiff.cpp" "%ROOT%src\common\Log.cpp" ^
  "%ROOT%src\common\AppResolver.cpp" "%ROOT%src\common\PinSettings.cpp" ^
  /link /DEBUG /DEF:"%ROOT%src\tap\UnglomTap.def" windowsapp.lib ole32.lib oleaut32.lib ^
  user32.lib shell32.lib propsys.lib advapi32.lib || goto :fail

echo.
echo === Unglom.exe ===
if exist "%OUT%\Unglom.exe" "%OUT%\Unglom.exe" --stop-this-copy
rc /nologo /i "%ROOT%res" /i "%ROOT%src\loader" /fo "%OUT%\obj\loader\Unglom.res" "%ROOT%src\loader\Unglom.rc" || goto :fail
cl %CFLAGS% /Fo"%OUT%\obj\loader\\" /Fd"%OUT%\obj\loader\\" /Fe"%OUT%\Unglom.exe" ^
  "%ROOT%src\loader\Unglom.cpp" "%ROOT%src\common\Log.cpp" ^
  "%ROOT%src\common\AppResolver.cpp" "%ROOT%src\common\PinSettings.cpp" "%OUT%\obj\loader\Unglom.res" ^
  /link /DEBUG /SUBSYSTEM:WINDOWS /MANIFEST:NO user32.lib shell32.lib advapi32.lib ole32.lib ^
  || goto :fail

echo.
echo === Running unit tests ===
"%OUT%\TitleDiffTests.exe" || goto :fail

echo.
echo Build succeeded. Output is in %OUT%
if /i not "%~1"=="nopause" pause
exit /b 0

:fail
echo.
echo BUILD FAILED
if /i not "%~1"=="nopause" pause
exit /b 1
