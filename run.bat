@echo off
setlocal

rem Define directories
set "OUTPUT_DIR=%~dp0execs"

echo ========================================
echo Starting Run Sequence
echo ========================================

rem Build the project first
call "%~dp0build.bat"
if errorlevel 1 (
    echo [ERROR] Build failed. Aborting execution.
    exit /b 1
)

echo Starting Server...
start "Redis Clone Server" "%OUTPUT_DIR%\server.exe"

echo Waiting for server to initialize...
timeout /t 2 /nobreak >nul

echo Starting Client (Python Test)...
call "%~dp0test.bat"

echo ========================================
echo Run Sequence Complete
echo ========================================

endlocal
exit /b 0