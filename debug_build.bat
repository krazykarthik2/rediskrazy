@echo off
gcc -O2 -Wall -Wextra src_c\dict.c src_c\rdb.c src_c\ae.c src_c\resp.c src_c\avl.c src_c\zset.c src_c\server.c -o execs\server.exe -lws2_32 > build_log.txt 2>&1
if %ERRORLEVEL% NEQ 0 (
    echo BUILD FAILED >> build_log.txt
) else (
    echo BUILD SUCCESS >> build_log.txt
)
dir execs\server.exe >> build_log.txt
