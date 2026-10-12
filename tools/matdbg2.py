import os, sys, struct
sys.path.insert(0, r"C:\Users\Kim Malale\Desktop\workdesk\raging thunder\tools")
import scn_parse as sp

scene = r"C:\Users\Kim Malale\Desktop\workdesk\raging thunder\extracted_clean\data\car\00\car.pxc"
sp.LOG.clear()
v, root, c, t = sp.parse_scene(scene)

found = []


def walk(o):
    if o is None:
        return
    m = o.get("mesh")
    if isinstance(m, dict):
        if m.get("tag") == "#RES":
            m = m.get("_next")
        if isinstance(m, dict) and m.get("geometry"):
            found.append((o.get("name"), m["geometry"]))
    for ch in o.get("children", []):
        walk(ch)


walk(root)
out = []
for name, g in found[:3]:
    stride = sp.calc_stride(g["vb_fmt"])
    out.append(f"mesh {name!r}: vb_fmt={g['vb_fmt']:#x} vb_count={g['vb_count']} stride={stride} vb_bytes={len(g['vb_data'])} expected={stride * g['vb_count']}")
    out.append(f"   ib_type={g['ib_type']} ib_count={g['ib_count']} elem={g['ib_elem']} ib_bytes={len(g['ib_data'])} expected={g['ib_elem'] * g['ib_count']}")
    out.append(f"   mats={len(g.get('materials') or [])} draws={len(g.get('draws') or [])} trailer={g.get('trailer')}")
out.append("LOG:")
out.extend(sp.LOG[:10])
open(r"C:\Users\Kim Malale\Desktop\workdesk\raging thunder\work\matdbg2.txt", "w").write("\n".join(out))
print("ok")
