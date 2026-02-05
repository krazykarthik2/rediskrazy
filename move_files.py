import os
import shutil

src = r"c:\Users\karthikkrazy\rediskrazy\src_c"
dst = r"c:\Users\karthikkrazy\rediskrazy\src_c\backend"

for f in os.listdir(src):
    if f.endswith(".c") or f.endswith(".h"):
        shutil.move(os.path.join(src, f), os.path.join(dst, f))
