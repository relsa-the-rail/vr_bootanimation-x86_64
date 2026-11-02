# vr_bootanimation preview (x86-64, native Wayland)

A re-implementation of Meta Quest 2's `/system/bin/vr_bootanimation`, rebuilt from the AArch64
binary (function names recovered from its embedded minidebuginfo, logic read from the
disassembly, shaders copied byte-for-byte). It plays the same `meta-animation-config.json` /
`.glb` / `meta.bin` files on your PC so you can preview a boot animation without flashing.

Nothing here contains Meta code or assets: it is a clean C++ port of the *behaviour*, built on
tinygltf (the same library the original uses), nlohmann/json and stb (vendored in `third_party/`).

## Build (CachyOS / Arch)
```fish
sudo pacman -S --needed cmake wayland wayland-protocols mesa libglvnd pkgconf
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
./build/vr_bootanimation -h
```
Debian/Ubuntu: `libwayland-dev wayland-protocols libegl-dev libgles-dev cmake pkg-config`.
A prebuilt binary (`bin/vr_bootanimation-x86_64`, built on Ubuntu 24.04/glibc 2.39) is included; it needs
`libwayland-client`, `libwayland-egl`, `libEGL`, `libGLESv2`, which any Wayland desktop already has.

## Use
The command line is the device's (`-d -m -c -f -s -h`) plus preview options. Device paths such as
`/system_ext/etc/bootanim/...` inside configs are remapped to your mounted image:

```fish
# the real config + assets from your mounted image (what the device would run with -c)
./build/vr_bootanimation -d -c /system_ext/etc/bootanim/meta-animation-config.json \
    --bootanim-dir /mnt/system_ext_a/etc/bootanim

# one file directly (like your earlier -m test)
./build/vr_bootanimation -d -m ~/xbox_boot.glb

# no -c/-m: the meta.bin + meta-colors.png logo path (what the binary does with no arguments)
./build/vr_bootanimation -d --bootanim-dir /mnt/system_ext_a/etc/bootanim
```
The window shows both eyes side by side (default 1832x960 = Quest 2's 3664x1920 panel / 2, same eye
aspect). Keys: **Esc/Q** quit, **Space** pause, **←/→** step one frame while paused, **R** restart,
**F** fullscreen, **S** screenshot (to `./traces`), **E** simulate `service.bootanim.exit`.

Offline / scripted (no window needed, `--headless`):
```fish
./build/vr_bootanimation -d -m test.glb --at 0,0.5,1,2,3 --shot out.png   # stills at animation times (s)
./build/vr_bootanimation -d -m test.glb --frames 120 --fps 30 --trace-dir frames   # deterministic frames
ffmpeg -framerate 30 -i frames/frame_%04d.png -pix_fmt yuv420p preview.mp4
```
Other options: `--size WxH`, `--fps N`, `--rotation DEG`, `--eye-centers L,R`, `--no-msaa`, `--fullscreen`,
`--system-ext DIR`, `--exit-after S`. `-f N` / `-s N` (demo mode) dump frames / per-eye crops like the original.

## Fidelity: what is and isn't reproduced
| Aspect | Status |
|---|---|
| Shaders (glTF + meta.bin) | byte-identical to the binary |
| GL state, MSAA 4x, RGBA8 + 24-bit depth | same as the device's EGL request |
| View matrix (stereo ±0.25 along camera X, `lookAt`), node TRS matrix | **executed in an emulator against the real code, diff < 3e-6** (`tools/re/`) |
| Projection (yfov/znear/zfar from glTF, aspect = (W/2)/H), viewports, scissors | read from disassembly |
| Animation timing: 0.25 s start delay, loop on input `max`, STEP/LINEAR, slerp | read from disassembly |
| meta.bin path: spin curve (0.5 s delay), IPD shift, aspect, colour-map lookup | read from disassembly |
| ~30 fps frame cap, exit/transition logic, 61 s "screen saving" fade | ported (demo mode disables exit) |
| Firmware-update progress bar | **not** reproduced (only shown during OTA) |
| Orthographic cameras, panel-specific eye centres | not reproduced (default 0.25/0.75; override with `--eye-centers`) |
| Adreno specifics (`mediump` = fp16, display pipeline, VrCompositor interplay) | **cannot** be reproduced on desktop |

"Accurate" therefore means: same geometry, timing, colour maths and quirks. Final brightness on the
headset also depends on the panel/compositor, which no desktop preview can capture.

See `docs/FINDINGS.md` for the device behaviours discovered (some change how GLBs must be authored).
`tools/make_test_assets.py` creates synthetic test files; `tools/re/` re-runs the verification.
