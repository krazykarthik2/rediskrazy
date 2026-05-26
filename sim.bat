@echo off
setlocal

echo ====================================================
echo STEP 0: Checking Prerequisites and Cleaning Up...
echo ====================================================

gcc --version >nul 2>&1
if errorlevel 1 (
    echo [ERROR] GCC compiler is not installed or not in PATH.
    echo Please install GCC to compile the C files.
    exit /b 1
)

python --version >nul 2>&1
if errorlevel 1 (
    echo [ERROR] Python is not installed or not in PATH.
    echo Please install Python 3 to run tests and the Python client wrapper.
    exit /b 1
)


echo Cleaning up any leftover server processes...
taskkill /F /IM server.exe /T >nul 2>&1
taskkill /F /IM qp_server.exe /T >nul 2>&1
echo Ready to build.

echo.
echo ====================================================
echo STEP 1: Building Project...
echo ====================================================
call build.bat
if errorlevel 1 (
    echo [ERROR] Build failed.
    exit /b 1
)

echo.
echo ====================================================
echo STEP 2: Running Automated Tests...
echo ====================================================
ping 127.0.0.1 -n 3 > nul
python test.py
if errorlevel 1 (
    echo [ERROR] Tests failed.
    exit /b 1
)

echo.
echo ====================================================
echo STEP 3: Starting Redis Server for CLI...
echo ====================================================
start /B execs\server.exe > server.log 2>&1
start /B execs\qp_server.exe > qp_server.log 2>&1
ping 127.0.0.1 -n 3 > nul

echo.
echo ====================================================
echo STEP 4: Choose Command Line Interface (CLI)
echo ====================================================
echo [1] Start C-based SQL CLI (sql_cli.exe)
echo [2] Start Python-based CLI Wrapper (librediskrazy)
echo ====================================================
echo.

set "choice=1"
set /p choice="Enter choice (1 or 2, default is 1): "
set choice=%choice: =%

if "%choice%"=="2" (
    echo.
    echo Starting Python CLI Wrapper...
    python -m librediskrazy.client
) else (
    echo.
    echo Starting SQL CLI Layer...
    execs\sql_cli.exe
)

echo.
echo Cleaning up...
taskkill /F /IM server.exe /T >nul 2>&1
taskkill /F /IM qp_server.exe /T >nul 2>&1

endlocal

