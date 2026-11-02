#!/usr/bin/env python3
"""Prepare raw code images + label files for Ghidra headless analysis.

    make_inputs.py REFERENCE_DIR SDK_LIB_DIR OUTDIR
    analyzeHeadless PROJDIR p -import OUTDIR/v2.bin -loader BinaryLoader \
        -loader-baseAddr 0x10000000 -processor ARM:LE:32:v5t \
        -scriptPath tools/re/ghidra -postScript Decomp.java OUTDIR/v2.labels OUTDIR/v2.c
"""
import os, re, struct, sys, glob
sys.path.insert(0, os.path.join(os.path.dirname(__file__), '..'))
from e32 import E32
from implib import ordinals

ref, libdir, out = sys.argv[1:4]
os.makedirs(out, exist_ok=True)
names = {}
for p in glob.glob(os.path.join(libdir, '*.lib')):
    names[os.path.basename(p)[:-4].upper()] = {o: re.sub(r'^_imp__', '', n) for o, n in ordinals(p).items()}
for v in range(1, 12):
    e = E32(os.path.join(ref, f'gamecomms.v{v}.dll'))
    open(os.path.join(out, f'v{v}.bin'), 'wb').write(e.code)
    lab = [(a, f'exp{i + 1:02d}') for i, a in enumerate(e.exports)]
    iat, idx = {}, 0
    for dll, ents in e.imports:
        short = dll.split('[')[0].split('.')[0].upper()
        for o in ents:
            n = names.get(short, {}).get(o)
            iat[e.text_size + 4 * idx] = f'{short}::{n}' if n else f'{short}_{o}'
            idx += 1
    c = e.code
    for i in range(0, e.text_size - 12, 4):
        w = struct.unpack_from('<4I', c, i)
        if w[:3] == (0xe59fc004, 0xe59cc000, 0xe12fff1c) and (w[3] - 0x10000000) in iat:
            lab.append((i, iat[w[3] - 0x10000000]))
    have = {a for a, _ in lab}
    for off, t in e.relocs:  # vtable targets that look like code
        if off >= len(c) - 4:
            continue
        w = struct.unpack_from('<I', c, off)[0] - 0x10000000
        if 0 <= w < e.text_size and w % 4 == 0 and w not in have:
            ins = struct.unpack_from('<I', c, w)[0]
            if (ins >> 16) == 0xe92d or ins == 0xe12fff1e or (ins & 0xfff00000) in (0xe3a00000, 0xe5900000, 0xe1a00000):
                lab.append((w, f'vf_{w:x}'))
                have.add(w)
    with open(os.path.join(out, f'v{v}.labels'), 'w') as f:
        f.write(f'TEXTSIZE {e.text_size:x}\n')
        for a, n in lab:
            f.write(f'{a + 0x10000000:x} {re.sub(r"[^A-Za-z0-9_:.$]", "_", n)}\n')
print('ok')
