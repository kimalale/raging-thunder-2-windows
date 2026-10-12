import os, sys, json
sys.path.insert(0, r"C:\Users\Kim Malale\Desktop\workdesk\raging thunder\tools")
import scn_parse as sp

scene = r"C:\Users\Kim Malale\Desktop\workdesk\raging thunder\extracted_clean\data\car\00\car.pxc"
sp.LOG.clear()
v, root, c, t = sp.parse_scene(scene)

lines = []
count = [0]


def walk(o):
    if o is None or count[0] > 8:
        return
    m = o.get("mesh")
    if isinstance(m, dict):
        if m.get("tag") == "#RES":
            m = m.get("_next")
        if isinstance(m, dict) and m.get("geometry"):
            g = m["geometry"]
            mats = g.get("materials") or []
            lines.append(f"mesh {o.get('name')!r} trailer={g.get('trailer')} mats={len(mats)} vb={g.get('vb_count')}")
            for mm in mats[:3]:
                tag = mm.get("tag") if isinstance(mm, dict) else "?"
                path = mm.get("path") if isinstance(mm, dict) else None
                nxt = None
                if isinstance(mm, dict) and mm.get("tag") == "#RES":
                    nx = mm.get("_next") or {}
                    nxt = (nx.get("tag"), nx.get("path"))
                lines.append(f"   mat tag={tag} path={path!r} next={nxt}")
            count[0] += 1
    for ch in o.get("children", []):
        walk(ch)


walk(root)
lines.append(f"LOG: {sp.LOG[:8]}")
open(r"C:\Users\Kim Malale\Desktop\workdesk\raging thunder\work\matdebug.txt", "w").write("\n".join(lines))
print("ok")
