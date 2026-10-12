import json, struct, os

d = r"C:\Users\Kim Malale\Desktop\workdesk\raging thunder\export\bin\Track_Tropical_1"
man = json.load(open(os.path.join(d, "scene.json")))
verts = open(os.path.join(d, "scene.verts.bin"), "rb").read()
idxs = open(os.path.join(d, "scene.idx.bin"), "rb").read()

camx, camz = 190.16, -85.92

for m in man["meshes"]:
    stride = 12 + (12 if m.get("has_n") else 0) + (8 if m.get("has_uv") else 0) + (4 if m.get("has_col") else 0)
    base_v = m["voff"]
    base_i = m["ioff"]
    for di, dr in enumerate(m.get("draws", [])):
        mat = man["materials"][dr["material"]]
        mn = [1e9] * 3
        mx = [-1e9] * 3
        for k in range(dr["count"]):
            idx = struct.unpack_from("<I", idxs, (base_i + dr["start"] + k) * 4)[0]
            off = base_v + idx * stride
            px, py, pz = struct.unpack_from("<3f", verts, off)
            mn[0] = min(mn[0], px); mx[0] = max(mx[0], px)
            mn[1] = min(mn[1], py); mx[1] = max(mx[1], py)
            mn[2] = min(mn[2], pz); mx[2] = max(mx[2], pz)
        sx, sy, sz = mx[0] - mn[0], mx[1] - mn[1], mx[2] - mn[2]
        cx, cy, cz = (mn[0] + mx[0]) / 2, (mn[1] + mx[1]) / 2, (mn[2] + mx[2]) / 2
        near = (mn[0] < camx + 60) and (mx[0] > camx - 60) and (mn[2] < camz + 60) and (mx[2] > camz - 60) and mn[1] < 12
        if near:
            print(f"{m['name']:>14} d{di} {mat['name']:<20} size=({sx:7.1f},{sy:6.1f},{sz:7.1f}) ctr=({cx:8.1f},{cy:6.1f},{cz:8.1f}) tris={dr['count']//3}")
