@echo off
rem CTR-AP support bundle. Double-click after a crash or a stuck seed: packs
rem the logs needed for a bug report into one zip (no game data, no passwords).
rem Default Windows PowerShell policy is Restricted; -File needs this process-only
rem bypass to run the bundled .ps1 on a default client without changing policy.
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0support-bundle.ps1"
pause
