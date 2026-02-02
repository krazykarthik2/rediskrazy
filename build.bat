@echo off
setlocal

rem Define directories
set "SOURCE_DIR=%~dp0src_c"
set "OUTPUT_DIR=%~dp0execs"

rem Create output directory if it doesn't exist
if not exist "%OUTPUT_DIR%" (
    echo Creating output directory: %OUTPUT_DIR%
    mkdir "%OUTPUT_DIR%"
)

echo ========================================
echo Building Redis Clone...
echo ========================================

rem Compile Server
echo Compiling server...
gcc -O2 -Wall -Wextra "%SOURCE_DIR%\dict.c" "%SOURCE_DIR%\resp.c" "%SOURCE_DIR%\server.c" -o "%OUTPUT_DIR%\server.exe" -lws2_32
if errorlevel 1 (
    echo [ERROR] Failed to compile server.
    exit /b 1
)
echo [SUCCESS] Server compiled to %OUTPUT_DIR%\server.exe

echo ========================================
echo Build Complete.
echo ========================================

endlocal
exit /b 0