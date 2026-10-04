@echo off
cd /d "%~dp0"
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0Install-PC98.ps1" %*
if errorlevel 1 (
  echo Installation failed. Read the message above.
  pause
  exit /b 1
)
echo Done. See README.md for building and ROM setup.
pause
