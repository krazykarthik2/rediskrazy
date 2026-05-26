@echo off
setlocal

rem Define directories
set "SOURCE_DIR=%~dp0src_c"
set "BACKEND_DIR=%SOURCE_DIR%\backend"
set "QP_DIR=%SOURCE_DIR%\query_processor"
set "UI_DIR=%SOURCE_DIR%\UI"
set "OUTPUT_DIR=%~dp0execs"

rem Create output directory if it doesn't exist
if not exist "%OUTPUT_DIR%" (
    echo Creating output directory: %OUTPUT_DIR%
    mkdir "%OUTPUT_DIR%"
)


rem delete all previous artifacts
echo ========================================
echo Deleting all previous artifacts...
echo ========================================
del /s /q "%OUTPUT_DIR%\*.*"

echo ========================================
echo Building Redis Clone with SQL Layer...
echo ========================================

rem Compile Server (Backend)
echo Compiling server...
pushd "%BACKEND_DIR%"
gcc -O2 -Wall -Wextra dict.c rdb.c ae.c resp.c avl.c zset.c tpool.c sds.c mempool.c expheap.c aofbuf.c server.c -o "%OUTPUT_DIR%\server.exe" -lws2_32
if errorlevel 1 (
    popd
    echo [ERROR] Failed to compile server.
    exit /b 1
)
popd
echo [SUCCESS] Server compiled to %OUTPUT_DIR%\server.exe

rem Compile SQL Processor Shared Library (DLL) for Python wrapper
echo Compiling SQL Processor DLL...
gcc -shared -O2 -Wall -Wextra "%QP_DIR%\sql_parser.c" "%QP_DIR%\sql_lexer.c" "%QP_DIR%\sql_parser_internal.c" "%QP_DIR%\sql_translator.c" "%QP_DIR%\schema_manager.c" "%QP_DIR%\py_interface.c" "%BACKEND_DIR%\sds.c" -o "%OUTPUT_DIR%\sql_processor.dll" -lws2_32
if errorlevel 1 (
    echo [ERROR] Failed to compile SQL Processor DLL.
    exit /b 1
)
echo [SUCCESS] SQL Processor DLL compiled to %OUTPUT_DIR%\sql_processor.dll

rem Compile Query Processor Server (qp_server)
echo Compiling Query Processor Server...
gcc -O2 -Wall -Wextra "%QP_DIR%\sql_parser.c" "%QP_DIR%\sql_lexer.c" "%QP_DIR%\sql_parser_internal.c" "%QP_DIR%\sql_translator.c" "%QP_DIR%\schema_manager.c" "%QP_DIR%\qp_server.c" "%BACKEND_DIR%\sds.c" -o "%OUTPUT_DIR%\qp_server.exe" -lws2_32
if errorlevel 1 (
    echo [ERROR] Failed to compile Query Processor Server.
    exit /b 1
)
echo [SUCCESS] Query Processor Server compiled to %OUTPUT_DIR%\qp_server.exe


rem Compile CLI (UI + Query Processor)
echo Compiling SQL CLI...
gcc -O2 -Wall -Wextra "%QP_DIR%\sql_parser.c" "%QP_DIR%\sql_lexer.c" "%QP_DIR%\sql_parser_internal.c" "%QP_DIR%\sql_translator.c" "%QP_DIR%\schema_manager.c" "%UI_DIR%\table_formatter.c" "%UI_DIR%\cli.c" "%BACKEND_DIR%\sds.c" -o "%OUTPUT_DIR%\sql_cli.exe" -lws2_32
if errorlevel 1 (
    echo [ERROR] Failed to compile SQL CLI.
    exit /b 1
)
echo [SUCCESS] SQL CLI compiled to %OUTPUT_DIR%\sql_cli.exe

echo ========================================
echo Build Complete.
echo ========================================

endlocal
exit /b 0