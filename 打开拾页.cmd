@echo off
if exist "%~dp0dist\Shiye\.shiye-update-pending.json" (
  powershell.exe -NoProfile -NonInteractive -ExecutionPolicy Bypass -File "%~dp0dist\Shiye\update-installer.ps1" -RecoverInstall "%~dp0dist\Shiye" -NoRestart
  if errorlevel 1 (
    pause
    exit /b 1
  )
)
start "" "%~dp0dist\Shiye\Shiye.exe" --data-dir "%~dp0data"

