#!/usr/bin/env python3
"""Generate synthetic test assets that mimic the structure of the Quest files documented in
CLAUDE.md (they are NOT Meta's assets):  test.glb, meta.bin, meta-colors.png, test-config.json.

test.glb : extruded asymmetric 'F' (so mirroring/orientation bugs are visible), COLOR_0 VEC4,
           1 morph target, camera at z=13 (yfov 45 deg, extras.depthCorrection=13),
           animation 4 s: node rotation (STEP, about Y) + morph weight (LINEAR)."""
import json, struct, math, sys, os, zlib
import numpy as np

out = sys.argv[1] if len(sys.argv) > 1 else "."
os.makedirs(out, exist_ok=True)

def box(x0, x1, y0, y1, z0, z1):
    P = []; I = []
    corners = [(x0,y0,z0),(x1,y0,z0),(x1,y1,z0),(x0,y1,z0),(x0,y0,z1),(x1,y0,z1),(x1,y1,z1),(x0,y1,z1)]
    faces = [((0,3,2,1),(0,0,-1)),((4,5,6,7),(0,0,1)),((0,4,7,3),(-1,0,0)),((1,2,6,5),(1,0,0)),((3,7,6,2),(0,1,0)),((0,1,5,4),(0,-1,0))]
    for quad, n in faces:
        b = len(P)
        for q in quad: P.append((corners[q], n))
        I += [b, b+1, b+2, b, b+2, b+3]
    return P, I

parts = [box(-0.30, -0.12, -0.33, 0.33, -0.10, 0.10),     # stem
         box(-0.12,  0.30,  0.15, 0.33, -0.10, 0.10),     # top bar
         box(-0.12,  0.18, -0.05, 0.10, -0.10, 0.10)]     # middle bar
pos, nrm, idx = [], [], []
for P, I in parts:
    b = len(pos)
    for (p, n) in P: pos.append(p); nrm.append(n)
    idx += [b + i for i in I]
pos = np.array(pos, np.float32); nrm = np.array(nrm, np.float32); idx = np.array(idx, np.uint32)
uv = np.zeros((len(pos), 2), np.float32)
t = (pos[:, 1] + 0.33) / 0.66                       # vertical gradient: blue (bottom) -> cyan/white (top)
col = np.stack([0.0 + 0.3 * t, 0.50 + 0.45 * t, 0.98 * np.ones_like(t), np.ones_like(t)], 1).astype(np.float32)
morph = np.zeros_like(pos); morph[:, 0] = -pos[:, 0] * 0.8; morph[:, 2] = pos[:, 2] * 1.5   # flatten x, thicken z

# animation: 4 s @120 Hz = 481 keys
N = 481
times = (np.arange(N) / 120.0).astype(np.float32)
ang = (np.arange(N) / (N - 1) * 2 * math.pi)
rot = np.stack([np.zeros(N), np.sin(ang / 2), np.zeros(N), np.cos(ang / 2)], 1).astype(np.float32)
# the STEP rotation reaches 2*pi at the last key (quaternion -w): fine for a looping test
wt_times = np.array([0.0, 2.0, 4.0], np.float32)
wts = np.array([0.0, 1.0, 0.0], np.float32)

blob = bytearray(); views = []; accs = []
def add(arr, target=None, type_="SCALAR", comp=5126, minmax=None):
    while len(blob) % 4: blob.append(0)
    off = len(blob); blob.extend(arr.tobytes())
    bv = {"buffer": 0, "byteOffset": off, "byteLength": arr.nbytes}
    if target: bv["target"] = target
    views.append(bv)
    a = {"bufferView": len(views) - 1, "componentType": comp, "count": int(arr.shape[0]), "type": type_}
    if minmax: a["min"], a["max"] = minmax
    accs.append(a); return len(accs) - 1

# interleaved POSITION/NORMAL/UV like the original (stride 32)
inter = np.concatenate([pos, nrm, uv], 1).astype(np.float32)
while len(blob) % 4: blob.append(0)
off = len(blob); blob.extend(inter.tobytes())
views.append({"buffer": 0, "byteOffset": off, "byteLength": inter.nbytes, "byteStride": 32, "target": 34962})
vb = len(views) - 1
accs.append({"bufferView": vb, "byteOffset": 0, "componentType": 5126, "count": len(pos), "type": "VEC3", "min": pos.min(0).tolist(), "max": pos.max(0).tolist()})
accs.append({"bufferView": vb, "byteOffset": 12, "componentType": 5126, "count": len(pos), "type": "VEC3"})
accs.append({"bufferView": vb, "byteOffset": 24, "componentType": 5126, "count": len(pos), "type": "VEC2"})
a_pos, a_nrm, a_uv = 0, 1, 2
a_col = add(col, 34962, "VEC4")
a_idx = add(idx, 34963, "SCALAR", 5125)
a_mt = add(morph, 34962, "VEC3", minmax=[morph.min(0).tolist(), morph.max(0).tolist()])
a_t = add(times, None, "SCALAR", minmax=[[0.0], [4.0]])
a_r = add(rot, None, "VEC4")
a_wt = add(wt_times, None, "SCALAR", minmax=[[0.0], [4.0]])
a_w = add(wts, None, "SCALAR")
for a in (accs[a_t], accs[a_wt]): a["min"], a["max"] = [0.0], [4.0]

gltf = {
 "asset": {"version": "2.0", "generator": "make_test_assets.py"},
 "scene": 0, "scenes": [{"nodes": [0, 2]}],
 "nodes": [
   {"name": "MetaLogo_180", "translation": [0, 0.208, 0], "children": [1]},
   {"name": "MetaLogo_Morph", "mesh": 0, "weights": [0.0]},
   {"name": "Camera", "camera": 0, "translation": [0, 0, 13]}],
 "cameras": [{"type": "perspective", "perspective": {"yfov": math.radians(45), "aspectRatio": 1, "znear": 0.1, "zfar": 100}, "extras": {"depthCorrection": 13}}],
 "meshes": [{"name": "meta_logo.PoseMorph", "primitives": [{"attributes": {"POSITION": a_pos, "NORMAL": a_nrm, "TEXCOORD_0": a_uv, "COLOR_0": a_col},
             "indices": a_idx, "material": 0, "targets": [{"POSITION": a_mt}]}], "weights": [0.0]}],
 "materials": [{"name": "VertexColor", "pbrMetallicRoughness": {"baseColorFactor": [1, 1, 1, 1], "metallicFactor": 1, "roughnessFactor": 1}, "emissiveFactor": [1, 1, 1]}],
 "animations": [{"samplers": [{"input": a_t, "output": a_r, "interpolation": "STEP"}, {"input": a_wt, "output": a_w, "interpolation": "LINEAR"}],
                 "channels": [{"sampler": 0, "target": {"node": 0, "path": "rotation"}}, {"sampler": 1, "target": {"node": 1, "path": "weights"}}]}],
 "accessors": accs, "bufferViews": views, "buffers": [{"byteLength": len(blob)}]}
if os.environ.get("SAMENODE"):   # both channels on node 0: the device keeps only the last channel
    gltf["animations"][0]["channels"][1]["target"]["node"] = 0
js = json.dumps(gltf, separators=(",", ":")).encode()
while len(js) % 4: js += b" "
while len(blob) % 4: blob.append(0)
glb = struct.pack("<III", 0x46546C67, 2, 12 + 8 + len(js) + 8 + len(blob)) + struct.pack("<II", len(js), 0x4E4F534A) + js + struct.pack("<II", len(blob), 0x004E4942) + bytes(blob)
open(os.path.join(out, "test_samenode.glb" if os.environ.get("SAMENODE") else "test.glb"), "wb").write(glb)

# ---- meta.bin : a 512x48 ribbon bent into a ring (positions ~ +-50 like the real file, x 0.000936 ~ 9% of an eye's width)
U, V = 512, 48
us, vs = np.meshgrid(np.arange(U), np.arange(V), indexing="ij")
th = us / (U - 1) * 2 * math.pi
rr = 7 * np.sin(vs / (V - 1) * math.pi)
x = (33 + rr * 0.6) * np.cos(th); z = (33 + rr * 0.6) * np.sin(th) * 0.45; y = 14 * (vs / (V - 1) - 0.5) * np.cos(2 * th) + 6 * (vs / (V - 1) - 0.5)
verts = np.stack([x, y, z, us / (U - 1), vs / (V - 1)], -1).reshape(-1, 5).astype(np.float32)
quads = []
for i in range(U - 1):
    for j in range(V - 1):
        a = i * V + j; b = a + V; c = b + 1; d = a + 1
        quads += [a, b, c, a, c, d]
quads = np.array(quads, np.uint32)
with open(os.path.join(out, "meta.bin"), "wb") as f:
    f.write(struct.pack("<i", len(verts))); f.write(verts.tobytes()); f.write(struct.pack("<i", len(quads))); f.write(quads.tobytes())

# ---- meta-colors.png : 1024x1 RGBA blue gradient (like the original palette: ~ (0,128,250))
g = np.linspace(0, 1, 1024)
px = np.stack([(0 + 80 * g), 128 + 60 * g, 250 - 30 * g, np.full_like(g, 255)], 1).astype(np.uint8)
def png(path, w, h, rgba):
    raw = b"".join(b"\x00" + rgba[y].tobytes() for y in range(h))
    def ch(t, d): c = struct.pack(">I", len(d)) + t + d; return c + struct.pack(">I", zlib.crc32(t + d) & 0xffffffff)
    open(path, "wb").write(b"\x89PNG\r\n\x1a\n" + ch(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 6, 0, 0, 0)) + ch(b"IDAT", zlib.compress(raw)) + ch(b"IEND", b""))
png(os.path.join(out, "meta-colors.png"), 1024, 1, px.reshape(1, 1024, 4))

json.dump({"bootanim": [{"path": "/system_ext/etc/bootanim/test.glb", "minimum-animation-loops": 0, "minimum-animation-time-seconds": 0}]},
          open(os.path.join(out, "test-config.json"), "w"))
print("wrote", os.listdir(out))
