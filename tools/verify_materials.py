import os, sys
sys.path.insert(0, r"C:\Users\Kim Malale\Desktop\workdesk\raging thunder\tools")
import scn_parse as sp

CASES = [
    r"C:\Users\Kim Malale\Desktop\workdesk\raging thunder\extracted_clean\data\car\00\car.pxc",
    r"C:\Users\Kim Malale\Desktop\workdesk\raging thunder\extracted_clean\data\track\Track_Tropical_1.scn",
]

out = []
for scene in CASES:
    sp.LOG.clear()
    v, root, c, t = sp.parse_scene(scene)
    meshes = []

    def walk(o):
        if o is None:
            return
        m = o.get("mesh")
        if isinstance(m, dict):
            if m.get("tag") == "#RES":
                m = m.get("_next")
            if isinstance(m, dict) and m.get("geometry"):
                meshes.append((o.get("name"), m["geometry"]))
        for ch in o.get("children", []):
            walk(ch)

    walk(root)
    out.append(f"=== {os.path.basename(scene)}: meshes={len(meshes)}")
    ok = 0
    issues = 0
    for name, g in meshes:
        mats = g.get("materials") or []
        draws = g.get("draws") or []
        ib = g.get("ib_count") or 0
        total = sum(d["indexCount"] for d in draws)
        bad_mi = [d for d in draws if d["material"] >= max(len(mats), 1)]
        if total == ib and not bad_mi:
            ok += 1
        else:
            issues += 1
            out.append(f"  ISSUE {name!r}: ib={ib} draws={len(draws)} sum={total} mats={len(mats)} badMatIdx={len(bad_mi)}")
        if len(mats):
            paths = sorted({(m.get("path") or "<none>") for m in mats})
            if len(out) < 40:
                out.append(f"  {name!r}: vb={g['vb_count']} ib={ib} mats={len(mats)} draws={len(draws)} shaders={sorted({m.get('shader') for m in mats})}")
                out.append(f"      tex: {paths[:4]}")
    out.append(f"  -> draw/index coverage OK: {ok}/{len(meshes)}, issues={issues}")

open(r"C:\Users\Kim Malale\Desktop\workdesk\raging thunder\work\verify_materials.txt", "w").write("\n".join(out))
print("written")
