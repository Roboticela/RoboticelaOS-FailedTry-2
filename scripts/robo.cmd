@echo off
REM RoboticelaOS pipeline launcher (cmd.exe / PowerShell prompt).
REM
REM   scripts\robo check
REM   scripts\robo build-all
REM   scripts\robo test-all
REM   scripts\robo validate-deployment
REM   scripts\robo run-qemu
REM
REM Prefers PowerShell 7 when present and otherwise falls back to Windows
REM PowerShell 5.1. The exit code is always the pipeline's own.

setlocal
set "PS1=%~dp0pipeline\robo.ps1"

if not exist "%PS1%" (
  echo robo: pipeline entry point not found: %PS1% 1>&2
  exit /b 2
)

where pwsh >nul 2>nul
if %errorlevel% equ 0 (
  pwsh -NoProfile -File "%PS1%" %*
  exit /b %errorlevel%
)

where powershell >nul 2>nul
if %errorlevel% equ 0 (
  powershell -NoProfile -ExecutionPolicy Bypass -File "%PS1%" %*
  exit /b %errorlevel%
)

echo robo: PowerShell not found. 1>&2
echo       RoboticelaOS builds on a Windows host with PowerShell 5.1+ or PowerShell 7+. 1>&2
exit /b 2
