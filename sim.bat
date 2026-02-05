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
timeout /t 2 /nobreak >nul
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
timeout /t 2 /nobreak >nul

echo.
echo ========================================
echo STEP 4: Starting SQL CLI Layer...
echo ========================================
echo You can run queries like:
echo   SELECT * FROM strings WHERE key = 'user:1'
echo   INSERT INTO strings VALUES ('user:1', 'Alice')
echo.
execs\sql_cli.exe

echo.
echo Cleaning up...
taskkill /F /IM server.exe /T >nul 2>&1

endlocal
