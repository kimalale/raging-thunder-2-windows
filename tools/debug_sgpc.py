import os, sys, struct
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import scn_parse as sp

ROOT = r"C:\Users\Kim Malale\Desktop\workdesk\raging thunder"
ASSETS = os.path.join(ROOT, "extracted_clean")

# 1) material path debug on car
sp.LOG.clear()
version, root, consumed, total = sp.parse_scene(os.path.join(ASSETS, r"data\car\00\car.pxc"))
found = []


def walk(o):
    if o is None:
        return
    m = o.get("mesh")
    if isinstance(m, dict):
        if m.get("tag") == "#RES":
            m = m.get("_next")
        if isinstance(m, dict) and m.get("tag") == "MESH" and m.get("geometry"):
            g = m["geometry"]
            mats = g.get("materials", [])
            found.append((o.get("name"), g.get("trailer"), len(mats),
                          [(x.get("tag"), (x.get("path") or (x.get("_next") or {}).get("path"))) for x in mats[:2]]))
    for c in o.get("children", []):
        walk(c)


walk(root)
for f in found[:8]:
    print(f, flush=True)
print("meshes found:", len(found), flush=True)

# 2) SGPC payload head of a track
b = open(os.path.join(ASSETS, r"data\track\Track_Tropical_1.scn"), "rb").read()
i = b.find(b"SGPC")
size = struct.unpack_from("<I", b, i + 4)[0]
p = i + 8
nlen = struct.unpack_from("<H", b, p)[0]
name = b[p + 2:p + 2 + nlen]
print("SGPC size", size, "name", name, flush=True)
q = p + 2 + nlen
flags = struct.unpack_from("<I", b, q)[0]
print("flags", hex(flags), flush=True)
q += 4
# spatial fields 76 bytes
real = struct.unpack_from("<i", b, q)[0] / 65536.0
q += 4
vec1 = struct.unpack_from("<3i", b, q)
q += 12
vec2 = struct.unpack_from("<3i", b, q)
q += 12
m43 = struct.unpack_from("<12i", b, q)
q += 48
print("real", real, "v1", [v / 65536 for v in vec1], "v2", [v / 65536 for v in vec2], flush=True)
print("m43", [round(v / 65536, 3) for v in m43], flush=True)
print("next 64 bytes:", b[q:q + 64].hex(" "), flush=True)
print("next u32s:", struct.unpack_from("<16I", b, q), flush=True)
