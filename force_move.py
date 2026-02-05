import os

src = r"c:\Users\karthikkrazy\rediskrazy\src_c"
dst = r"c:\Users\karthikkrazy\rediskrazy\src_c\backend"

if not os.path.exists(dst):
    os.makedirs(dst)

for f in os.listdir(src):
    if f.endswith(".c") or f.endswith(".h"):
        sp = os.path.join(src, f)
        dp = os.path.join(dst, f)
        with open(sp, 'rb') as f_src:
            content = f_src.read()
        with open(dp, 'wb') as f_dst:
            f_dst.write(content)
        os.remove(sp)
