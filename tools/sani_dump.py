import os, sys, json
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import scn_parse

root = r"C:\Users\Kim Malale\Desktop\workdesk\raging thunder\extracted_clean"
targets = ["Tracks/Track_Tropical_1.scn", "Tracks/Track_Tropical_2.scn", "Cars/Viper.scn"]
if len(sys.argv) > 1:
    targets = sys.argv[1:]

def walk(o, path):
    name = o.get("name") or o.get("tag", b"?").decode("latin1") if isinstance(o.get("tag", ""), bytes) else o.get("name", "")
    tag = o.get("tag")
    if isinstance(tag, bytes):
        tag = tag.decode("latin1")
    me = name or tag
    if tag == "SANI":
        fl = o.get("flags")
        tls = o.get("timelines", [])
        wps = o.get("waypoints", [])
        print("=" * 70)
        print("SANI under path: " + " / ".join(path))
        print("  flags=0x%x" % fl)
        for i, t in enumerate(tls):
            print("  TL%d active=%s value=%.4f speed=%.4f limit=%.4f mode=%d" % (i, t["active"], t["value"], t["speed"], t["limit"], t["mode"]))
        if wps:
            print("  waypoints (%d):" % len(wps))
            for w in wps:
                print("    (%.1f, %.1f, %.1f)" % w)
    for c in o.get("children", []) or []:
        walk(c, path + [me])

def scan(o, path):
    if isinstance(o, dict):
        if o.get("tag") == "SANI":
            print("FOUND SANI at path: " + " / ".join(path))
            print("  " + json.dumps({k: v for k, v in o.items() if k not in ("children",)}, default=str)[:500])
        if o.get("dynamics") is not None:
            d = o["dynamics"]
            if isinstance(d, dict):
                print("DYN under %s: tag=%r name=%r" % (" / ".join(path) or "(root)", d.get("tag"), d.get("name")))
        me = o.get("name") or o.get("tag", "?")
        for k, v in o.items():
            if k == "children":
                for c in v or []:
                    scan(c, path + [str(me)])
            elif isinstance(v, (dict, list)):
                scan(v, path + [str(me) + "." + k])
    elif isinstance(o, list):
        for it in o:
            scan(it, path)

for t in targets:
    p = os.path.join(root, t)
    if not os.path.isfile(p):
        print("missing " + p)
        continue
    print("### " + t)
    version, scene, consumed, total = scn_parse.parse_scene(p)
    scan(scene, [])

