"""Export RT2 scenes to OBJ + MTL + PNG (world-space, Unity-import friendly)."""
import os, sys, struct, shutil

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import scn_parse as sp

ROOT = r"C:\Users\Kim Malale\Desktop\workdesk\raging thunder"
ASSETS = os.path.join(ROOT, "extracted_clean")
OUT = os.path.join(ROOT, "export", "obj")

try:
    from PIL import Image
    HAVE_PIL = True
except Exception:
    HAVE_PIL = False


def find_texture(path):
    if not path:
        return None
    rel = path.replace("\\", "/").lstrip("/")
    cands = [rel, rel.replace(".pvr", ".dds"), rel.replace(".pvr", ".png")]
    if not rel.lower().startswith("data/"):
        cands += ["data/" + rel, "data/" + rel.replace(".pvr", ".dds")]
    for cand in cands:
        fp = os.path.join(ASSETS, *cand.split("/"))
        if os.path.isfile(fp):
            return fp
    base = os.path.splitext(os.path.basename(rel))[0].lower()
    for dirpath, dirs, files in os.walk(os.path.join(ASSETS, "data")):
        for f in files:
            if os.path.splitext(f)[0].lower() == base:
                return os.path.join(dirpath, f)
    return None


def mat4_mul(a, b):
    """row-major 4x4 multiply."""
    r = [0.0] * 16
    for i in range(4):
        for j in range(4):
            r[i * 4 + j] = sum(a[i * 4 + k] * b[k * 4 + j] for k in range(4))
    return r


def m43_to_m44(m):
    return [m[0], m[1], m[2], 0.0,
            m[3], m[4], m[5], 0.0,
            m[6], m[7], m[8], 0.0,
            m[9], m[10], m[11], 1.0]


def xform_point(m, p):
    x, y, z = p
    return (m[0] * x + m[1] * y + m[2] * z + m[12],
            m[4] * x + m[5] * y + m[6] * z + m[13],
            m[8] * x + m[9] * y + m[10] * z + m[14])


def xform_dir(m, p):
    x, y, z = p
    return (m[0] * x + m[1] * y + m[2] * z,
            m[4] * x + m[5] * y + m[6] * z,
            m[8] * x + m[9] * y + m[10] * z)


class ObjWriter:
    def __init__(self):
        self.v = []
        self.vt = []
        self.vn = []
        self.faces = []  # (group, mat, [(vi,ti,ni),...])
        self.mats = {}
        self.tex_png = {}

    def texture_png(self, tex_path, out_dir):
        if not tex_path:
            return None
        if tex_path in self.tex_png:
            return self.tex_png[tex_path]
        src = find_texture(tex_path)
        name = None
        if src:
            stem = os.path.splitext(os.path.basename(src))[0]
            name = stem + ".png"
            dest = os.path.join(out_dir, name)
            if not os.path.isfile(dest):
                try:
                    if HAVE_PIL:
                        im = Image.open(src).convert("RGBA")
                        im.save(dest)
                    else:
                        shutil.copy(src, dest)
                except Exception:
                    name = None
        self.tex_png[tex_path] = name
        return name

    def material(self, tex_path, out_dir):
        import re
        safe = re.sub(r"[^A-Za-z0-9_.\\/-]", "_", tex_path or "")
        if safe.lower().endswith((".pvr", ".dds", ".png")):
            name = os.path.splitext(os.path.basename(safe))[0] or "default"
            name = re.sub(r"[^A-Za-z0-9_.-]", "_", name)
        else:
            name = "default"
            safe = None
        if name in self.mats:
            return name
        png = self.texture_png(safe, out_dir) if safe else None
        self.mats[name] = png
        return name


FLIP_V = False  # verified against engine texture convention (car livery test, CarTest scene)


def add_mesh(writer, geo, world, group, default_tex, out_dir):
    positions, normals, uvs = sp.decode_vertices(geo["vb_data"], geo["vb_fmt"], geo["vb_count"], sp.LOG)
    if not positions:
        return 0
    elem = geo["ib_elem"] or 2
    if elem == 2 and len(geo["ib_data"]) >= 2 * geo["ib_count"]:
        indices = list(struct.unpack_from("<%dH" % geo["ib_count"], geo["ib_data"]))
    elif elem == 4 and len(geo["ib_data"]) >= 4 * geo["ib_count"]:
        indices = list(struct.unpack_from("<%dI" % geo["ib_count"], geo["ib_data"]))
    else:
        indices = list(geo["ib_data"][:geo["ib_count"]])
    if not indices:
        return 0
    draws = geo.get("draws") or []
    mats = geo.get("materials") or []
    segments = []
    if draws:
        for d in draws:
            mi = d["material"] if d["material"] < len(mats) else 0
            tex = mats[mi].get("path") if mats else None
            segments.append((d["indexStart"], d["indexCount"], d["baseVertex"], tex))
    else:
        tex = None
        for m in mats:
            if isinstance(m, dict) and m.get("path"):
                tex = m["path"]
                break
        segments.append((0, len(indices), 0, tex or default_tex))
    base_v = len(writer.v) + 1
    base_t = len(writer.vt) + 1
    base_n = len(writer.vn) + 1
    for p in positions:
        writer.v.append(xform_point(world, p))
    if uvs:
        for (u, v) in uvs:
            writer.vt.append((u, 1.0 - v if FLIP_V else v))
    if normals:
        for n in normals:
            writer.vn.append(xform_dir(world, n))
    tris = 0
    has_uv = bool(uvs)
    has_n = bool(normals)
    for (start, count, bv, tex) in segments:
        seg = indices[start:start + count]
        if not seg:
            continue
        mat = writer.material(tex or default_tex, out_dir)
        tri = []
        for i in seg:
            vi = base_v + i
            ti = base_t + i if has_uv else 0
            ni = base_n + i if has_n else 0
            tri.append((vi, ti, ni))
        for k in range(0, len(tri) - 2, 3):
            writer.faces.append((group, mat, has_uv, has_n, tri[k], tri[k + 1], tri[k + 2]))
            tris += 1
    return tris


def walk_obj(writer, obj, world, out_dir, default_tex, stats, skip=None):
    mat = obj.get("matrix")
    if mat:
        world = mat_mul_safe(world, m43_to_m44(mat))
    name = obj.get("name") or obj.get("tag") or "node"
    if skip is not None and skip(name or ""):
        return
    geo = obj.get("geometry")
    if isinstance(geo, dict):
        stats[0] += add_mesh(writer, geo, world, name, default_tex, out_dir)
    shape = obj.get("mesh")
    if obj.get("tag") == "SGPS" and isinstance(shape, dict):
        if shape.get("tag") == "#RES":
            shape = shape.get("_next")
        if isinstance(shape, dict) and shape.get("tag") in ("MESH", "LICM") and shape.get("geometry"):
            stats[0] += add_mesh(writer, shape["geometry"], world, name, default_tex, out_dir)
    for c in obj.get("children", []):
        walk_obj(writer, c, world, out_dir, default_tex, stats, skip)


def mat_mul_safe(a, b):
    return mat4_mul(a, b)


def collect_routes(writer, obj, world, routes):
    mat = obj.get("matrix")
    if mat:
        world = mat4_mul(world, m43_to_m44(mat))
    pts = obj.get("points")
    if pts:
        routes.append([xform_point(world, p) for p in pts])
    for c in obj.get("children", []):
        collect_routes(writer, c, world, routes)


def convert(scn_path, skip=None, suffix=""):
    sp.LOG.clear()
    name = os.path.splitext(os.path.basename(scn_path))[0] + suffix
    out_dir = os.path.join(OUT, name)
    os.makedirs(out_dir, exist_ok=True)
    version, root, consumed, total = sp.parse_scene(scn_path)
    writer = ObjWriter()
    stats = [0]
    walk_obj(writer, root, [1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1], out_dir, None, stats, skip)
    routes = []
    collect_routes(writer, root, [1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1], routes)
    if routes:
        import json as _json
        with open(os.path.join(out_dir, name + ".route.json"), "w") as rf:
            _json.dump({"routes": routes}, rf)
    obj_path = os.path.join(out_dir, name + ".obj")
    mtl_path = os.path.join(out_dir, name + ".mtl")
    with open(mtl_path, "w", encoding="ascii", errors="replace") as mf:
        for mname, png in writer.mats.items():
            mf.write(f"newmtl {mname}\n")
            mf.write("Kd 0.8 0.8 0.8\nKa 0.2 0.2 0.2\nKs 0 0 0\nNs 10\n")
            if png:
                mf.write(f"map_Kd {png}\n")
            mf.write("\n")
    with open(obj_path, "w", encoding="ascii", errors="replace") as of:
        of.write(f"mtllib {name}.mtl\n")
        for v in writer.v:
            of.write(f"v {v[0]:.4f} {v[1]:.4f} {v[2]:.4f}\n")
        for t in writer.vt:
            of.write(f"vt {t[0]:.6f} {t[1]:.6f}\n")
        for n in writer.vn:
            of.write(f"vn {n[0]:.4f} {n[1]:.4f} {n[2]:.4f}\n")
        for f in writer.faces:
            group, mat, has_uv, has_n, a, b, c = f
            of.write(f"g {group}\nusemtl {mat}\n")
            if has_uv and has_n:
                of.write(f"f {a[0]}/{a[1]}/{a[2]} {b[0]}/{b[1]}/{b[2]} {c[0]}/{c[1]}/{c[2]}\n")
            elif has_uv:
                of.write(f"f {a[0]}/{a[1]} {b[0]}/{b[1]} {c[0]}/{c[1]}\n")
            elif has_n:
                of.write(f"f {a[0]}//{a[2]} {b[0]}//{b[2]} {c[0]}//{c[2]}\n")
            else:
                of.write(f"f {a[0]} {b[0]} {c[0]}\n")
    print(f"{name}: verts={len(writer.v)} tris={stats[0]} materials={len(writer.mats)} textures={sum(1 for p in writer.mats.values() if p)}", flush=True)


def stock_variant_skip(name):
    """Keep LOD0 and upgrade level 0; drop other LOD/upgrade variants and trails.
    Naming scheme observed in the game data: '<part>_Lod<n>' and 'upgrade_<part>_<n>'."""
    import re as _re
    if not name:
        return False
    low = name.lower()
    if low.startswith("vapour") or low.startswith("trail"):
        return True
    m = _re.match(r"^(.*)_Lod(\d+)$", name, _re.IGNORECASE)
    if m:
        return int(m.group(2)) > 0
    m = _re.match(r"^upgrade_.*_(\d+)$", low)
    if m:
        return int(m.group(1)) != 0
    return False


if __name__ == "__main__":
    args = []
    for a in sys.argv[1:]:
        if a == "--flip":
            FLIP_V = True
        else:
            args.append(a)
    targets = args
    if not targets:
        for dirpath, dirs, files in os.walk(os.path.join(ASSETS, "data")):
            for f in sorted(files):
                if f.lower().endswith((".scn", ".pxc")):
                    targets.append(os.path.join(dirpath, f))
    for t in targets:
        try:
            skip = stock_variant_skip if t.lower().endswith("car.pxc") else None
            convert(t, skip, "_flip" if FLIP_V else "")
        except Exception as e:
            import traceback
            traceback.print_exc()
            print("FAILED", t, e, flush=True)
