@echo off
rem CTR-AP support bundle. Double-click after a crash or a stuck seed: packs
rem the logs needed for a bug report into one zip (no game data, no passwords).
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0support-bundle.ps1"
pause
