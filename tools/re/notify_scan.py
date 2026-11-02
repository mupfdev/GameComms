#!/usr/bin/env python3
"""List the MGameBTCommsNotify vtable offsets each variant calls.

Usage: notify_scan.py DISASM_DIR   (files gamecomms.vN.dll.s from e32dis.py)
The notify pointer lives at +0x1c of the internal comms object; a call is
'ldr r0,[rX,#0x1c]' followed by 'ldr r12,[r3|r12,#off]; mov lr,pc; bx r12'.
"""
import re, sys, os
for v in range(1, 12):
    L = open(os.path.join(sys.argv[1], f'gamecomms.v{v}.dll.s')).read().split('\n')
    offs = set()
    for i, l in enumerate(L):
        if re.search(r'ldr\s+r0, \[r\d+, #0x1c\]', l):
            for j in range(i + 1, min(i + 10, len(L))):
                m = re.search(r'ldr\w*\s+r12, \[r(?:12|3), #(0x[0-9a-f]+)\]', L[j])
                if m:
                    offs.add(int(m.group(1), 16))
                    break
    print(f'v{v}:', ' '.join(hex(o) for o in sorted(offs)))
