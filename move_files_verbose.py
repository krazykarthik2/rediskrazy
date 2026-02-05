import os
import shutil
import sys

src = r"c:\Users\karthikkrazy\rediskrazy\src_c"
dst = r"c:\Users\karthikkrazy\rediskrazy\src_c\backend"

print(f"Moving from {src} to {dst}")
if not os.path.exists(dst):
    print(f"Creating {dst}")
    os.makedirs(dst)

files = os.listdir(src)
print(f"Found {len(files)} items in {src}")

for f in files:
    if f.endswith(".c") or f.endswith(".h"):
        sf = os.path.join(src, f)
        df = os.path.join(dst, f)
        print(f"Moving {sf} -> {df}")
        try:
            shutil.move(sf, df)
        except Exception as e:
            print(f"Error moving {f}: {e}")
