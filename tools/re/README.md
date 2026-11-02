# Reverse engineering tools

| Script | Purpose |
|---|---|
| `e32.py` | EKA1 E32 image parser (`python e32.py file.dll` prints header and imports) |
| `implib.py` | ordinal → symbol table of an SDK import library (`euser.lib` …) |
| `dis.py` | annotated disassembly via llvm-objdump (`dis.py file.dll outdir`) |
| `compact.py` | compacts the disassembly (virtual calls, common EUSER names) |
| `vtables.py` | lists GCC 2.9 vtables found through relocations |
| `xmatch.py` | compares exports between variants (`GC_DIS=dir xmatch.py 2 3 4 …`, needs `c<N>.s` from `dis.py` + sed compaction) |
| `notify_scan.py` | lists notify-interface vtable offsets each variant calls |
| `ghidra/make_inputs.py` + `ghidra/Decomp.java` | Ghidra headless decompilation of all variants with import names |

See `docs/REVERSE_ENGINEERING.md` for the results.
