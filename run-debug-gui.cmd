@echo off
setlocal
cd /d "%~dp0"

if not exist "x64\Debug\remote_debug_gui.exe" (
  echo remote_debug_gui.exe was not found.
  echo Build Debug x64 in Visual Studio first.
  pause
  exit /b 1
)

start "" "%~dp0x64\Debug\remote_debug_gui.exe"
