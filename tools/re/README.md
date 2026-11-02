# Reverse-engineering / verification scripts

Everything in `src/` was written from the AArch64 code of `vr_bootanimation`. These scripts let you
re-check that work on your own copy of the binary (not included here).

```
cp /path/to/vr_bootanimation .            # the stripped ARM64 binary from system_ext/bin or system/bin
pip install capstone pyelftools unicorn --break-system-packages    # or in a venv
python3 extract_symbols.py vr_bootanimation   # -> dbg.elf, syms.txt (the binary keeps a minidebuginfo!)
python3 adis.py "Renderer::draw()"        # annotated disassembly (resolves PLT imports + rodata strings)
python3 adis.py 0x27944 0x279c4           # or an address range
python3 test_view.py                      # emulates the view-matrix block of Renderer::draw with Unicorn
python3 test_tomat.py                     # emulates NodeTransformation::toMat4
```

`test_view.py` / `test_tomat.py` run the *real* instructions in Unicorn on random inputs and compare
against the formulas used in `src/vmath.h` / `src/renderer.cpp`. Expected output: max difference
below ~1e-5 (float rounding).
