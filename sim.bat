@echo off
setlocal

echo ========================================
echo STEP 1: Building Project...
echo ========================================
call build.bat
if errorlevel 1 (
    echo [ERROR] Build failed.
    exit /b 1
)

echo.
echo ========================================
echo STEP 2: Running Automated Tests...
echo ========================================
ping 127.0.0.1 -n 3 > nul
python test.py
if errorlevel 1 (
    echo [ERROR] Tests failed.
    exit /b 1
)

echo.
echo ========================================
echo STEP 3: Starting Redis Server for CLI...
echo ========================================
start /B execs\server.exe
ping 127.0.0.1 -n 3 > nul

echo.
echo ========================================
echo STEP 4: Starting SQL CLI Layer...
echo ========================================
echo You can run queries
echo.
execs\sql_cli.exe

echo.
echo Cleaning up...
taskkill /F /IM server.exe /T >nul 2>&1

endlocal
