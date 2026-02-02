@echo off
echo Running test_v2.py...
python TESTING\test_v2.py > test_debug.txt 2>&1
echo Done.
type test_debug.txt
