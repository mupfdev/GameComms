"""Cross-variant export matcher: compares normalised instruction streams of
each exported function between DLL variants."""
import sys, os, re, difflib, struct
sys.path.insert(0, os.path.dirname(__file__))
from e32 import E32
REF = os.environ.get('GC_REF', os.path.join(os.path.dirname(__file__), '..', '..', 'reference'))
DIS = os.environ.get('GC_DIS', 'dis')

def funcs(v):
    e = E32(os.path.join(REF, f'gamecomms.v{v}.dll'))
    lines = open(os.path.join(DIS, f'c{v}.s')).read().split('\n')
    idx = {}
    for i, l in enumerate(lines):
        m = re.match(r'([0-9a-f]+) ', l)
        if m: idx[int(m.group(1), 16)] = i
    res = {}
    for o, a in enumerate(e.exports, 1):
        i = idx.get(a)
        if i is None: continue
        toks = []
        for l in lines[i:i+60]:
            m = re.match(r'([0-9a-f]+) (.*)', l)
            if not m: continue
            ins = m.group(2)
            if ins.startswith(('andne', 'andeq', 'strhne', 'ldrdne', 'strdne', '<unknown>', 'cdp', 'addseq')): continue
            ins = re.sub(r'0x[0-9a-f]+ <([A-Z]+_\d+)>', r'<\1>', ins)
            ins = re.sub(r'0x[0-9a-f]+ ?(<[^>]*>)?', 'X', ins)
            ins = re.sub(r';.*', '', ins).strip()
            ins = re.sub(r'<(EUSER|BTENG|ESOCK|CONE|EIKCOCTL|BAFL|BLUETOOTH|BTDEVICE|BTMANCLIENT|SDPAGENT|SDPDATABASE|EIKCORE)_\d+>', r'<\1>', ins)
            toks.append(ins)
            if ins.startswith(('bx lr', 'pop {r4', 'ldm sp!')) and len(toks) > 2 and 'bx' in ins: break
        res[o] = toks
    return res

if __name__ == '__main__':
    base = int(sys.argv[1])
    B = funcs(base)
    for v in [int(x) for x in sys.argv[2:]]:
        F = funcs(v)
        row = []
        for o in sorted(F):
            if o in B:
                r = difflib.SequenceMatcher(None, B[o], F[o]).ratio()
            else:
                r = -1
            row.append((o, r))
        bad = [(o, round(r, 2)) for o, r in row if r < 0.95]
        print(f'v{v} vs v{base}: {len(F)} exports, {sum(1 for o,r in row if r>=0.95)} match>=0.95; differing: {bad}')
