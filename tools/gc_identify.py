#!/usr/bin/env python3
"""Identify GAMECOMMS.DLL variants and the GameComms functions a game uses.

    gc_identify.py dll  path/to/gamecomms.dll      -> variant, API, ordinals
    gc_identify.py game path/to/game.app [...]      -> imported GameComms ordinals with names

The second form answers whether a game uses any of the exported helper
classes (see docs/COMPATIBILITY.md); run it on the .app of an installed
game together with the gamecomms.dll shipped next to it.
"""
import hashlib, os, sys
ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), '..'))
sys.path.insert(0, os.path.join(ROOT, 'tools', 're'))
sys.path.insert(0, os.path.join(ROOT, 'dll', 'variants'))
from e32 import E32
from variants import VARIANTS

def variant_of(path):
    md5 = hashlib.md5(open(path, 'rb').read()).hexdigest()
    for v, (m, api, table) in VARIANTS.items():
        if m == md5:
            return v, api, table
    e = E32(path)
    cands = [v for v, (_, _, t) in VARIANTS.items() if len(t) == e.export_count]
    return (cands[0] if cands else None), None, (VARIANTS[cands[0]][2] if cands else {})

def main():
    if len(sys.argv) < 3:
        print(__doc__); return 2
    if sys.argv[1] == 'dll':
        v, api, t = variant_of(sys.argv[2])
        print(f'variant v{v}  API {api}.x  {len(t)} ordinals' if v else 'unknown variant')
        return 0
    for p in sys.argv[2:]:
        e = E32(p)
        dll = os.path.join(os.path.dirname(p), 'gamecomms.dll')
        v, api, table = variant_of(dll) if os.path.exists(dll) else (None, None, {})
        for name, ords in e.imports:
            if name.upper().startswith('GAMECOMMS'):
                print(f'{os.path.basename(p)}: imports {len(ords)} GameComms ordinals' + (f' (v{v})' if v else ''))
                for o in ords:
                    desc = table.get(o, ('?', '?', '?'))[1] if table else ''
                    helper = '  <-- helper class' if 'internal helper' in desc else ''
                    print(f'  {o:3d} {desc}{helper}')
    return 0

if __name__ == '__main__':
    sys.exit(main())
