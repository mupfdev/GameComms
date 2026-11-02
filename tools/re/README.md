# Reverse engineering tools

Scripts used to analyse the original DLLs. Results are in
`docs/REVERSE_ENGINEERING.md`.

| Script | Purpose |
|---|---|
| `e32.py` | E32 image parser. `python e32.py file.dll` prints header and imports. |
| `implib.py` | Ordinal to symbol table of an SDK import library such as `euser.lib`. |
| `e32dis.py` | Annotated disassembly with llvm-objdump. `python e32dis.py file.dll outdir` |
| `compact.py` | Shortens the disassembly (virtual calls, common EUSER names). |
| `vtables.py` | Lists vtables found through relocations. |
| `xmatch.py` | Compares exported functions between variants. `GC_DIS=dir python xmatch.py 2 3 4` |
| `notify_scan.py` | Lists the callback offsets each variant calls. |
| `ghidra/make_inputs.py`, `ghidra/Decomp.java` | Ghidra headless decompilation of all variants. |
