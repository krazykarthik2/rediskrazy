import subprocess
import sys
import os

print("Starting Build...")
is_windows = os.name == 'nt'

# Define sources and targets using cross-platform paths
src_dir = "src_c/backend"
sources = [
    os.path.join(src_dir, f) for f in [
        "dict.c", "rdb.c", "ae.c", "resp.c", "avl.c", "zset.c",
        "tpool.c", "sds.c", "mempool.c", "expheap.c", "aofbuf.c", "server.c"
    ]
]

exec_name = "server.exe" if is_windows else "server"
target_path = os.path.join("execs", exec_name)

cmd = ["gcc", "-O2", "-Wall", "-Wextra"] + sources + ["-o", target_path]

if is_windows:
    cmd.append("-lws2_32")
else:
    cmd.extend(["-pthread", "-lm"])

result = subprocess.run(cmd, capture_output=True, text=True)

if result.returncode != 0:
    print("BUILD FAILED:")
    print(result.stderr)
    sys.exit(1)
else:
    print("BUILD SUCCESS")
    print(f"Server compiled to {target_path}")

print("Starting Tests...")
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
