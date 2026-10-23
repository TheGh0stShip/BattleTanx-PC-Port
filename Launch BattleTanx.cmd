@echo off
setlocal DisableDelayedExpansion
if "%~1"=="" (
    "%~dp0BattleTanx.exe"
) else (
    "%~dp0BattleTanx.exe" "%~1"
)
if errorlevel 1 pause
exit /b %ERRORLEVEL%
