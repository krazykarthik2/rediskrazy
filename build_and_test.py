import subprocess
import sys
import os

print("Starting Build...")
cmd = [
    "gcc", "-O2", "-Wall", "-Wextra",
    r"src_c\dict.c", r"src_c\rdb.c", r"src_c\ae.c", r"src_c\resp.c", r"src_c\avl.c", r"src_c\zset.c", r"src_c\tpool.c", r"src_c\server.c",
    "-o", r"execs\server.exe", "-lws2_32"
]
result = subprocess.run(cmd, capture_output=True, text=True)

if result.returncode != 0:
    print("BUILD FAILED:")
    print(result.stderr)
    sys.exit(1)
else:
    print("BUILD SUCCESS")
    print("Server compiled to execs\\server.exe")

print("Starting Tests...")
# Flush stdout before creating subprocess
sys.stdout.flush()

test_proc = subprocess.run(["python", "-u", "test.py"], capture_output=True, text=True)
print(test_proc.stdout)
if test_proc.stderr:
    print("STDERR:")
    print(test_proc.stderr)

if test_proc.returncode != 0:
    print("TESTS FAILED")
    sys.exit(1)

print("TESTS PASSED")
