@echo off
setlocal

rem Define directories
set "OUTPUT_DIR=%~dp0execs"

echo ========================================
echo Starting Run Sequence
echo ========================================

echo Starting Server...
start "Redis Clone Server" "%OUTPUT_DIR%\server.exe"

endlocal
exit /b 0