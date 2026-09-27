#pragma once

#include <string>

namespace unglom {

// %LOCALAPPDATA%\Unglom, created on first use.
std::wstring DataDir();

// Opens (appending) %LOCALAPPDATA%\Unglom\<fileName>. Until called, Log() is a no-op.
void LogInit(const wchar_t* fileName, const wchar_t* tag);

void Log(_Printf_format_string_ const wchar_t* format, ...);

}  // namespace unglom
