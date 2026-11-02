#!/usr/bin/env python3
"""Pull the xz-compressed minidebuginfo (.gnu_debugdata) out of the stripped vr_bootanimation,
write dbg.elf and syms.txt (demangled function names + addresses).  Usage:
    python3 extract_symbols.py /path/to/vr_bootanimation
Needs: python3, xz, binutils `nm` (any arch).  Then run the other scripts in this directory
from the same folder with a copy/symlink of the binary named 'vr_bootanimation'."""
import struct, subprocess, sys
path = sys.argv[1] if len(sys.argv) > 1 else "vr_bootanimation"
d = open(path, "rb").read()
shoff = struct.unpack_from("<Q", d, 0x28)[0]
shentsize, shnum, shstrndx = struct.unpack_from("<HHH", d, 0x3A)
secs = []
for i in range(shnum):
    o = shoff + i * shentsize
    name, typ, flags, addr, off, size = struct.unpack_from("<IIQQQQ", d, o)
    secs.append((name, off, size))
stroff = secs[shstrndx][1]
def nm(i):
    s = stroff + secs[i][0]; return d[s:d.index(b"\0", s)].decode()
for i in range(shnum):
    if nm(i) == ".gnu_debugdata":
        _, off, size = secs[i]
        open("dbg.xz", "wb").write(d[off:off + size])
        subprocess.run("xz -dc dbg.xz > dbg.elf", shell=True, check=True)
        out = subprocess.run(["nm", "-C", "--defined-only", "dbg.elf"], capture_output=True, text=True).stdout
        open("syms.txt", "w").write(out)
        print("wrote dbg.elf and syms.txt (%d symbols)" % out.count("\n")); break
else:
    sys.exit("no .gnu_debugdata section")
