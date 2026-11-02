#!/usr/bin/env python3
"""Disassemble an EKA1 E32 DLL with llvm-objdump and annotate imports/exports/literals."""
import sys, os, struct, subprocess, re, tempfile
sys.path.insert(0, os.path.dirname(__file__))
from e32 import E32

def build(path, outdir, names=None):
    e = E32(path)
    names = names or {}
    base = 0  # ELF addresses == code offsets; absolute values are rebased by RB
    RB = e.code_base
    c = e.code
    syms = {}
    # exports
    for i, a in enumerate(e.exports):
        syms[a] = names.get(i + 1, f'exp{i+1:02d}')
    # IAT slots
    iat = {}
    idx = 0
    for dll, ents in e.imports:
        short = dll.split('[')[0].split('.')[0]
        for ordv in ents:
            iat[base + e.text_size + 4 * idx] = f'{short}_{ordv}'
            idx += 1
    e.iat = iat
    # thunks
    for i in range(0, e.text_size - 12, 4):
        w = struct.unpack_from('<4I', c, i)
        if w[0] == 0xe59fc004 and w[1] == 0xe59cc000 and w[2] == 0xe12fff1c and (w[3]-RB) in iat:
            syms[base + i] = iat[w[3]-RB]
    raw = os.path.join(outdir, os.path.basename(path) + '.bin')
    open(raw, 'wb').write(c[:e.text_size])
    elf = raw + '.elf'
    args = ['llvm-objcopy', '-I', 'binary', '-O', 'elf32-littlearm',
            '--rename-section', '.data=.text,alloc,load,readonly,code,contents',
            '--strip-symbol', '_binary_' + re.sub(r'[^A-Za-z0-9]', '_', raw) + '_start']
    args += [raw, elf]
    subprocess.check_call(args)
    out = subprocess.check_output(['llvm-objdump', '-d', '--no-show-raw-insn', '--triple=armv5te', elf], text=True)
    relocset = set(off for off, t in e.relocs)
    lines = []
    for ln in out.splitlines():
        m = re.match(r'\s*([0-9a-f]+):\s+(.*)', ln)
        if m:
            addr = int(m.group(1), 16); ins = m.group(2)
            mm = re.search(r'\[pc, #(-?(?:0x[0-9a-f]+|\d+))\]', ins)
            if mm and ins.startswith('ldr'):
                la = addr + 8 + int(mm.group(1), 0)
                off = la - base
                if 0 <= off < len(c):
                    v = struct.unpack_from('<I', c, off)[0]
                    note = f'={v:#x}'
                    if off in relocset: v -= RB
                    if v in syms: note += f' <{syms[v]}>'
                    if base <= v < base + len(c):
                        s = c[v - base:v - base + 48]
                        # try narrow/wide string or descriptor
                        note += ' ' + describe(c, v - base)
                    ins += '\t; ' + note
            ins = re.sub(r'<\.text\+0x([0-9a-f]+)>', lambda m_: '<' + syms.get(int(m_.group(1), 16), '.+' + m_.group(1)) + '>', ins)
            ln = f'{addr:08x}:  {ins}'
            if addr in syms:
                lines.append(f'\n{syms[addr]}:')
        lines.append(ln)
    return e, syms, '\n'.join(lines)

def describe(c, off):
    s = c[off:off + 80]
    # _LIT16 descriptor: TInt length (type 0 in top 4 bits) followed by utf16
    if len(s) >= 6:
        ln = struct.unpack_from('<I', s, 0)[0]
        if 0 < ln < 36 and all(s[4 + 2*i + 1] == 0 and 32 <= s[4 + 2*i] < 127 for i in range(min(ln, 37))):
            return 'LIT16"' + s[4:4 + 2*ln].decode('utf-16le') + '"'
        if 0 < ln < 70 and all(32 <= x < 127 for x in s[4:4 + ln]):
            return 'LIT8"' + s[4:4 + ln].decode() + '"'
    t = s.split(b'\0')[0]
    if len(t) >= 3 and all(32 <= x < 127 or x in (9, 10, 13) for x in t):
        return 'STR"' + t.decode() + '"'
    return ''

if __name__ == '__main__':
    path, outdir = sys.argv[1], sys.argv[2]
    e, syms, txt = build(path, outdir)
    open(os.path.join(outdir, os.path.basename(path) + '.s'), 'w').write(txt)
    print('ok', len(txt.splitlines()))
