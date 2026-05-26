import subprocess
import sys
import os

print("Starting Build...")
cmd = [
    "gcc", "-O2", "-Wall", "-Wextra",
    r"src_c\backend\dict.c", r"src_c\backend\rdb.c", r"src_c\backend\ae.c",
    r"src_c\backend\resp.c", r"src_c\backend\avl.c", r"src_c\backend\zset.c",
    r"src_c\backend\tpool.c", r"src_c\backend\sds.c", r"src_c\backend\mempool.c",
    r"src_c\backend\expheap.c", r"src_c\backend\aofbuf.c", r"src_c\backend\server.c",
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
