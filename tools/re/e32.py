#!/usr/bin/env python3
"""Minimal EKA1 (Symbian 6.x) E32Image parser for uncompressed images."""
import struct, sys

class E32:
    def __init__(self, path):
        self.path = path
        self.b = open(path, 'rb').read()
        h = struct.unpack_from('<' + 'I'*31, self.b, 0)
        (self.uid1, self.uid2, self.uid3, self.check, self.sig, self.cpu, self.cksum_code,
         self.cksum_data, self.version, self.time_lo, self.time_hi, self.flags, self.code_size,
         self.data_size, self.heap_min, self.heap_max, self.stack, self.bss, self.entry,
         self.code_base, self.data_base, self.dllref_count, self.export_off, self.export_count,
         self.text_size, self.code_off, self.data_off, self.import_off, self.coderel_off,
         self.datarel_off, self.prio) = h
        assert self.sig == 0x434f5045, 'not EPOC'
        self.code = self.b[self.code_off:self.code_off + self.code_size]
        self.exports = [struct.unpack_from('<I', self.b, self.export_off + 4*i)[0]
                        for i in range(self.export_count)]
        self.parse_imports()
        self.parse_relocs()

    def parse_imports(self):
        off = self.import_off
        size = struct.unpack_from('<I', self.b, off)[0]
        p = off + 4
        self.imports = []  # (dllname, [(iat_code_offset, ordinal)])
        for _ in range(self.dllref_count):
            name_off, n = struct.unpack_from('<Ii', self.b, p); p += 8
            name = self.b[off + name_off:].split(b'\0')[0].decode()
            entries = []
            for i in range(n):
                entries.append(struct.unpack_from('<I', self.b, p)[0]); p += 4
            self.imports.append((name, entries))

    def parse_relocs(self):
        self.relocs = []
        if not self.coderel_off: return
        off = self.coderel_off
        size, count = struct.unpack_from('<II', self.b, off)
        p = off + 8; end = p + size
        while p < end:
            page, bsize = struct.unpack_from('<II', self.b, p)
            for i in range((bsize - 8)//2):
                e = struct.unpack_from('<H', self.b, p + 8 + 2*i)[0]
                if e & 0xf000:
                    self.relocs.append((page + (e & 0xfff), e >> 12))
            p += bsize

if __name__ == '__main__':
    e = E32(sys.argv[1])
    print(f'{sys.argv[1]}: uid3={e.uid3:#x} flags={e.flags:#x} code={e.code_size:#x} text={e.text_size:#x} data={e.data_size:#x} bss={e.bss:#x} exports={e.export_count} entry={e.entry:#x} ver={e.version:#x}')
    for name, ents in e.imports:
        print(' ', name, ents)
