import os, sys, struct
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import scn_parse as sp

ASSETS = r"C:\Users\Kim Malale\Desktop\workdesk\raging thunder\extracted_clean"
scene = os.path.join(ASSETS, r"data\track\Track_Tropical_1.scn")
sp.LOG.clear()
version, root, consumed, total = sp.parse_scene(scene)
print("version", hex(version), "consumed", consumed, "/", total)
print("LOG lines:", len(sp.LOG))
for l in sp.LOG[:20]:
    print("  !", l)

stats = {"meshes": 0, "geo_ok": 0, "geo_fail": 0, "verts": 0, "idx": 0}
sizes = []


def walk(o, depth=0):
    if o is None:
        return
    t = o.get("tag")
    if t == "MESH":
        stats["meshes"] += 1
        g = o.get("geometry")
        if g:
            pos, nor, uv = sp.decode_vertices(g["vb_data"], g["vb_fmt"], g["vb_count"], sp.LOG)
            if pos:
                stats["geo_ok"] += 1
                stats["verts"] += len(pos)
                stats["idx"] += g["ib_count"]
            else:
                stats["geo_fail"] += 1
                sizes.append((g["vb_fmt"], g["vb_count"], g["ib_count"], len(g["vb_data"])))
        else:
            stats["geo_fail"] += 1
    for c in o.get("children", []):
        walk(c, depth + 1)
    m = o.get("mesh")
    if isinstance(m, dict):
        if m.get("tag") == "#RES":
            m = m.get("_next")
        if isinstance(m, dict) and m.get("tag") == "MESH":
            walk(m, depth + 1)


walk(root)
print(stats)
print("failed samples (fmt, vb_count, ib_count, vb_bytes):")
for s in sizes[:10]:
    print("  ", s)
print("LOG tail:")
for l in sp.LOG[-12:]:
    print("  !", l)
