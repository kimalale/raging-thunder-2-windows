import os, sys, struct
sys.path.insert(0, r"C:\Users\Kim Malale\Desktop\workdesk\raging thunder\tools")
import scn_parse as sp

FIXED = 65536.0
scene = r"C:\Users\Kim Malale\Desktop\workdesk\raging thunder\extracted_clean\data\car\00\car.pxc"
sp.LOG.clear()
v, root, c, t = sp.parse_scene(scene)

# find body mesh with 4 materials
target = None


def walk(o):
    global target
    if o is None or target:
        return
    m = o.get("mesh")
    if isinstance(m, dict):
        if m.get("tag") == "#RES":
            m = m.get("_next")
        if isinstance(m, dict) and m.get("geometry"):
            g = m["geometry"]
            if len(g.get("materials") or []) == 4:
                target = g
    for ch in o.get("children", []):
        walk(ch)


walk(root)
g = target
out = []
out.append(f"materials={len(g['materials'])} draws={len(g['draws'])} trailer={g['trailer']}")
for i, mm in enumerate(g["materials"]):
    out.append(f"mat{i}: path={mm.get('path')} shader={mm.get('shader')} flags={mm.get('flags'):#x} color={mm.get('color'):#010x} reals={mm.get('reals')} tail32={mm.get('tail32'):#x}")
for i, d in enumerate(g["draws"]):
    out.append(f"draw{i}: {d}")
open(r"C:\Users\Kim Malale\Desktop\workdesk\raging thunder\work\carmat_dump.txt", "w").write("\n".join(out))
print("done")
