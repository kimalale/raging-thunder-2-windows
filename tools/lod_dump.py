import os, sys
sys.path.insert(0, r"C:\Users\Kim Malale\Desktop\workdesk\raging thunder\tools")
import scn_parse as sp

scene = r"C:\Users\Kim Malale\Desktop\workdesk\raging thunder\extracted_clean\data\car\00\car.pxc"
sp.LOG.clear()
v, root, c, t = sp.parse_scene(scene)

rows = []


def walk(o, path):
    if o is None:
        return
    name = o.get("name") or o.get("tag")
    p = path + "/" + name
    if o.get("tag") == "SGPS":
        vals = o.get("shape_vals") or []
        rows.append((p, [round(x, 3) for x in vals]))
    for ch in o.get("children", []):
        walk(ch, p)


walk(root, "")
out = []
for p, vals in rows:
    out.append(f"{p}\n    {vals}")
open(r"C:\Users\Kim Malale\Desktop\workdesk\raging thunder\work\lod_dump.txt", "w").write("\n".join(out))
print("SGPS count:", len(rows))
