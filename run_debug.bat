@echo off
call build.bat > build_log.txt 2>&1
echo Build finished. >> build_log.txt
python -u test.py > test_log.txt 2>&1
echo Test finished. >> test_log.txt
type build_log.txt
echo ---
type test_log.txt
