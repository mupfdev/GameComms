import sys, os, struct
sys.path.insert(0, os.path.dirname(__file__))
from e32 import E32
e = E32(sys.argv[1]); c = e.code; RB = e.code_base
rel = sorted(off for off, t in e.relocs)
rs = set(rel)
# vtable candidates: GCC2 thunk vtables begin with two zero words then function pointers (relocated)
i = 0
out = []
for off in range(0, e.text_size - 12, 4):
    w0, w1 = struct.unpack_from('<II', c, off)
    if w0 == 0 and w1 == 0 and (off + 8) in rs:
        n = 0; ents = []
        p = off + 8
        while p in rs:
            v = struct.unpack_from('<I', c, p)[0] - RB
            if not (0 <= v < e.text_size): break
            ents.append(v); p += 4
        if len(ents) >= 2:
            out.append((off, ents))
for off, ents in out:
    print(f'vtable @{off:#06x}: ' + ' '.join(f'{x:#x}' for x in ents))
