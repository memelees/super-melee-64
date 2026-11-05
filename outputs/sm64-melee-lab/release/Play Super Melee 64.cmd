@echo off
if not exist "%~dp0Super Melee 64.exe" (
  echo Run Setup Windows.cmd first to build Super Melee 64.
  pause
  exit /b 1
)
start "" "%~dp0Super Melee 64.exe"
