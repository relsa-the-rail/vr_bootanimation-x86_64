# What the binary actually does (and what it means for custom animations)

Source: `vr_bootanimation` (ARM64, stripped, but with `.gnu_debugdata` minidebuginfo giving ~700
function names). **[disasm]** = read from disassembly, **[emu]** = executed in an emulator and compared,
**[?]** = inferred / untested on a headset.

## Which render path runs  (answers CLAUDE.md §9 q.2) [disasm]
Decided by the command line, not by which files exist:
- `-c cfg.json` or `-m file.glb` -> glTF `Renderer` (up to 2 models, played one after another).
- neither -> `LogoAnimator` (`meta.bin` + `meta-colors.png`, hard-coded `/system_ext/etc/bootanim/`).

Which one the device uses depends on how init starts it: `grep -r vr_bootanimation /mnt/*/etc/init/`.
Config format: `{"bootanim":[{"path":"...glb","minimum-animation-loops":N,"minimum-animation-time-seconds":N}]}`;
the second model starts once the first has met both minimums.

## Lighting (q.3) [disasm]
Unlit. The fragment shader has `// #define SHADING` commented out:
`out = uBaseColor * (uUseVertexColor ? vec4(COLOR_0.rgb,1) : 1) * (texture if any)`.
Emissive, metallic, roughness, normals are ignored. No gamma/tone-mapping in the shader (the window buffer is
tagged BT.709/sRGB full-range, so COLOR_0 values are shown as sRGB-encoded values).

## Camera / size (q.4) [disasm][emu]
- Camera = first camera node in the scene graph; **only that node's own** translation/rotation are used
  (parent transforms and animation are ignored). Projection uses its `yfov/znear/zfar` but the aspect is
  `(W/2)/H` of the real window, so the camera's `aspectRatio` is ignored.
- Eyes are the camera shifted ±0.25 along its local X (eye 0 at -0.25). Per-eye viewport/scissor = left/right half.
- `extras.depthCorrection = D` -> `y *= 1 - z_world/D` after the model matrix, before view/projection
  (compensates vertical perspective for geometry off the z=0 plane). At z=0 it does nothing.
- At z=13, yfov 45° the eye shows 10.8 units vertically; the 1.0-wide Meta logo covers ~9.7 % of an eye's width,
  and the `meta.bin` path (100 units * 0.000936) gives ~9.4 %: the two paths are designed to match.

## Timing [disasm]
- Clock starts when the `Renderer` object is constructed, i.e. **model loading counts**. Animation time is
  `max(elapsed - 0.25 s, 0)`, wrapped modulo the input accessor's `max[0]` (so **input accessors must have
  `min`/`max`**: without `max` the period is 0). `LogoAnimator` uses a 0.5 s delay and `x=t*2π/3.3 s`,
  `y=x-sin x`, angle `=(y+2(y-sin y))/6`.
- Draw loop capped at ~30 fps (33.3 ms).
- Only animation #0 is played; interpolation must be `STEP` or `LINEAR` (CUBICSPLINE is rejected).

## Constraints a custom GLB must respect [disasm]  <- important
1. **One morph target only.** Only `targets[0]` is bound (`inPosition1`/`inNormal1`); the shader computes
   `pos + target0 * uWeight` with a single weight per node (`node.weights[0]` or the animated weight).
2. **Weights channel assumes a single target:** the output accessor is read as one float per keyframe, so a
   `weights` sampler for N>1 targets (N floats per key) is mis-read.
3. **One animated channel per node.** Each channel first *resets* the node to its static TRS/weight and then applies
   itself, so if a node has both `rotation` and `weights` channels, only the **last** channel in the list survives.
   Put rotation and morph weight on different nodes (like the original: node 0 rotates, child mesh node morphs).
4. Nodes must use TRS (`matrix` is ignored). Skins/sparse accessors/KHR extensions are not used; Draco is rejected.
5. Every bufferView that feeds attributes/indices needs `target` 34962/34963, otherwise it is never uploaded.
6. Primitives must be **indexed** (the safety check rejects `indices == -1`); all accessor/bufferView indices must be valid.
7. Textures: only `baseColorTexture` (+ `baseColorFactor`); material-less primitive without COLOR_0 is drawn grey 0.5.

## Implications for the "OG Xbox" build (CLAUDE.md §10) [disasm]
It uses 4 morph targets and rotation + weights channels; with the rules above that is risky on device:
only target 0 would apply, the flat `weights` accessor would be misread, and any node with both rotation and
weights loses one. Safer authoring: one mesh node per animated object, each with a **single** morph target and its
own `weights` channel (scalar, 1 float per key), rotations on separate parent nodes, `min`/`max` on time accessors.

## Not decoded / open
- Exact sound path (this binary links no audio libs).
- Exit properties beyond `service.bootanim.exit[_vendor]` + `ro.boot.bootanimation_overrun_timeout`.
- Whether `-m/-d` works on a live system with VrCompositor running.
