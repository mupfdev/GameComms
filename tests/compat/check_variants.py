#!/usr/bin/env python3
"""Compatibility checks for every GAMECOMMS.DLL variant.

1. The export table of each variant (dll/variants/variants.py) matches the
   original: same number of ordinals, contiguous, nothing missing.  The
   original DLLs in reference/ are not part of the repository (they are
   Nokia's files).  When they are present they are checked too; otherwise
   the values recorded from them below are used.
2. Every thunk referenced by a table exists in dll/symbian/GcExports.cpp
   with the argument count the original function uses.
3. Optional: built replacement DLLs (--built DIR containing vN/gamecomms.dll)
   are valid E32 DLL images with the original UIDs and export count.

Exit status 0 when all checks pass.
"""
import argparse, hashlib, os, re, sys
ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), '..', '..'))
sys.path.append(os.path.join(ROOT, 'dll', 'variants'))
sys.path.append(os.path.join(ROOT, 'tools', 're'))
from variants import VARIANTS          # noqa: E402
from e32 import E32                    # noqa: E402

# Recorded from the original DLLs (reference/gamecomms.vN.dll).
ORIG_UIDS = (0x10000079, 0x1000008d, 0x101f5ee2)
ORIG_EXPORTS = {1: 33, 2: 55, 3: 56, 4: 56, 5: 57, 6: 57, 7: 57, 8: 57, 9: 57, 10: 63, 11: 63}

# number of register/stack arguments (including "this") observed in the
# original export code for each implemented function kind
ARGC = {
    'gc1_NewL': 3, 'gc2_NewL': 3, 'gc1_StartHostL': 3, 'gc2_StartHostL': 3, 'gc2_StartClientL': 3,
    'gc1_SendDataToClient': 3, 'gc2_SendDataTo': 3, 'gc2_DisconnectClient': 3, 'gc1_dtor': 2, 'gc2_dtor': 2,
    'rsge_dtor': 2, 'gc2_DeclareRemoteDevice': 2, 'gc2_ConnectClient': 2, 'gc2_SendData': 2,
    'gc1_SendDataToAllClients': 2, 'gc1_SendDataToHost': 2, 'gc1_DisconnectClient': 2,
    'gc2_SetHostAcceptMode': 2, 'gc2_BluetoothPowerState': 2, 'gc2_SetBluetoothPowerState': 2,
}

def thunks():
    src = open(os.path.join(ROOT, 'dll', 'symbian', 'GcExports.cpp')).read()
    res = {}
    for m in re.finditer(r'GC_EXPORT\s+[\w\s\*&]+?\b(\w+)\(([^)]*)\)', src):
        args = [a for a in m.group(2).split(',') if a.strip() and a.strip() != 'void']
        res[m.group(1)] = len(args)
    return res

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--built', help='directory with vN/gamecomms.dll')
    a = ap.parse_args()
    ok = True
    th = thunks()
    for v, (md5, api, table) in VARIANTS.items():
        errs = []
        uids, count = ORIG_UIDS, ORIG_EXPORTS[v]
        ref = os.path.join(ROOT, 'reference', f'gamecomms.v{v}.dll')
        if os.path.exists(ref):
            data = open(ref, 'rb').read()
            if hashlib.md5(data).hexdigest() != md5:
                errs.append(f'{ref} has an unexpected md5')
            orig = E32(ref)
            if (orig.uid1, orig.uid2, orig.uid3) != uids or orig.export_count != count:
                errs.append('recorded UIDs or export count differ from the original')
            src = 'original'
        else:
            src = 'recorded'
        if count != len(table):
            errs.append(f'export count {len(table)} != original {count}')
        if sorted(table) != list(range(1, count + 1)):
            errs.append('ordinals not contiguous')
        for o, (sym, proto, conf) in table.items():
            if sym not in th:
                errs.append(f'ordinal {o}: thunk {sym} missing')
            elif sym in ARGC and th[sym] != ARGC[sym]:
                errs.append(f'ordinal {o}: {sym} takes {th[sym]} args, original {ARGC[sym]}')
        if a.built:
            p = os.path.join(a.built, f'v{v}', 'gamecomms.dll')
            if not os.path.exists(p):
                errs.append(f'{p} missing')
            else:
                b = E32(p)
                if (b.uid1, b.uid2, b.uid3) != uids:
                    errs.append(f'UIDs {b.uid1:#x}/{b.uid2:#x}/{b.uid3:#x} differ from original')
                if b.export_count != count:
                    errs.append(f'built DLL exports {b.export_count}, original {count}')
                if any(x == 0 for x in b.exports):
                    errs.append('built DLL has empty export slots')
        ok &= not errs
        print(f'v{v:<2} api {api} {count:2d} ordinals ({src}): ' + ('OK' if not errs else '; '.join(errs)))
    return 0 if ok else 1

if __name__ == '__main__':
    sys.exit(main())
