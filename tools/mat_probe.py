import os, sys, struct
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import scn_parse

path = os.path.join(r"C:\Users\Kim Malale\Desktop\workdesk\raging thunder\extracted_clean", "data/track/Track_Tropical_1.scn")
version, root, consumed, total = scn_parse.parse_scene(path)

def find_first_mesh(o):
    g = o.get("geometry")
    if isinstance(g, dict) and g.get("materials"):
        return g
    shape = o.get("mesh")
    if isinstance(shape, dict):
        s = shape.get("_next") if shape.get("tag") == "#RES" else shape
        if isinstance(s, dict) and isinstance(s.get("geometry"), dict) and s["geometry"].get("materials"):
            return s["geometry"]
    for c in o.get("children", []):
        r = find_first_mesh(c)
        if r:
            return r
    return None

g = find_first_mesh(root)
print("mesh materials:", len(g["materials"]))
for i, m in enumerate(g["materials"]):
    print("%2d %-40s shader=%d flags=0x%04x mode=%d aref=%.3f reals=%s" % (
        i, m["path"], m["shader"], m["flags"], m.get("mode", -1), m.get("alpha_ref", 0), m["reals"]))
