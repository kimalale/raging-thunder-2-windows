"""Convert RT2 .scn/.pxc scenes to glTF 2.0 (meshes + transforms + textures)."""
import os, sys, json, struct, base64, shutil

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import scn_parse as sp

ROOT = r"C:\Users\Kim Malale\Desktop\workdesk\raging thunder"
ASSETS = os.path.join(ROOT, "extracted_clean")
OUT = os.path.join(ROOT, "export")
TEXCACHE = os.path.join(OUT, "_tex")

os.makedirs(TEXCACHE, exist_ok=True)

try:
    from PIL import Image
    HAVE_PIL = True
except Exception:
    HAVE_PIL = False
    print("WARNING: no Pillow; textures skipped")


def find_texture(path):
    if not path:
        return None
    rel = path.replace("\\", "/").lstrip("/")
    for cand in (rel, rel.replace(".pvr", ".dds"), rel.replace(".pvr", ".png"),
                 rel.replace("data/", "data/"), "data/" + rel):
        fp = os.path.join(ASSETS, *cand.split("/"))
        if os.path.isfile(fp):
            return fp
    base = os.path.basename(rel)
    stem = os.path.splitext(base)[0]
    for dirpath, dirs, files in os.walk(os.path.join(ASSETS, "data")):
        for f in files:
            if os.path.splitext(f)[0].lower() == stem.lower():
                return os.path.join(dirpath, f)
    return None


def tex_to_png(path):
    if not HAVE_PIL or not path:
        return None
    name = os.path.splitext(os.path.basename(path))[0] + ".png"
    dest = os.path.join(TEXCACHE, name)
    if os.path.isfile(dest):
        return name
    try:
        im = Image.open(path)
        im = im.convert("RGBA")
        im.save(dest)
        return name
    except Exception as e:
        print("tex fail", path, e)
        return None


def fixup_matrix(m):
    """4x3 row-major (last row = translation) -> glTF column-major 16."""
    r0 = m[0:3]
    r1 = m[3:6]
    r2 = m[6:9]
    r3 = m[9:12]
    return [
        r0[0], r1[0], r2[0], 0.0,
        r0[1], r1[1], r2[1], 0.0,
        r0[2], r1[2], r2[2], 0.0,
        r3[0], r3[1], r3[2], 1.0,
    ]


class Builder:
    def __init__(self):
        self.bin = bytearray()
        self.bufferViews = []
        self.accessors = []
        self.meshes = []
        self.nodes = []
        self.materials = []
        self.images = []
        self.textures = []
        self.material_cache = {}

    def add_view(self, data, target=None):
        off = len(self.bin)
        self.bin += data
        while len(self.bin) % 4:
            self.bin += b"\x00"
        bv = {"buffer": 0, "byteOffset": off, "byteLength": len(data)}
        if target:
            bv["target"] = target
        self.bufferViews.append(bv)
        return len(self.bufferViews) - 1

    def add_accessor(self, view, comp_type, count, type_, minmax=None, target=None):
        acc = {"bufferView": view, "componentType": comp_type, "count": count, "type": type_}
        if minmax:
            acc["min"], acc["max"] = minmax
        self.accessors.append(acc)
        return len(self.accessors) - 1

    def material_for(self, tex_path):
        key = tex_path or "__none__"
        if key in self.material_cache:
            return self.material_cache[key]
        mat = {"name": os.path.basename(tex_path) if tex_path else "default",
               "pbrMetallicRoughness": {"baseColorFactor": [0.8, 0.8, 0.8, 1.0],
                                        "metallicFactor": 0.0, "roughnessFactor": 0.8}}
        png = tex_to_png(tex_path)
        if png:
            if png not in self.images:
                self.images.append(png)
            idx = self.images.index(png)
            self.textures.append({"source": idx, "sampler": 0})
            mat["pbrMetallicRoughness"]["baseColorTexture"] = {"index": len(self.textures) - 1}
            mat["doubleSided"] = True
        self.materials.append(mat)
        self.material_cache[key] = len(self.materials) - 1
        return self.material_cache[key]

    def mesh_from_geometry(self, geo, name):
        positions, normals, uvs = sp.decode_vertices(geo["vb_data"], geo["vb_fmt"], geo["vb_count"], sp.LOG)
        if not positions:
            return None
        elem = geo["ib_elem"] or 2
        if elem == 2:
            indices = list(struct.unpack_from("<%dH" % geo["ib_count"], geo["ib_data"]))
        elif elem == 4:
            indices = list(struct.unpack_from("<%dI" % geo["ib_count"], geo["ib_data"]))
        else:
            indices = list(geo["ib_data"])
        if not indices:
            return None
        # texture path from first material
        tex = None
        for m in geo.get("materials", []):
            if isinstance(m, dict):
                if m.get("path"):
                    tex = find_texture(m["path"])
                    break
                if m.get("tag") == "TEXT" and m.get("path"):
                    tex = find_texture(m["path"])
                    break
        mat_idx = self.material_for(tex)
        pos_b = b"".join(struct.pack("<3f", *p) for p in positions)
        pv = self.add_view(pos_b, 34962)
        pmin = [min(p[i] for p in positions) for i in range(3)]
        pmax = [max(p[i] for p in positions) for i in range(3)]
        pacc = self.add_accessor(pv, 5126, len(positions), "VEC3", [pmin, pmax])
        attrs = {"POSITION": pacc}
        if normals:
            nb = b"".join(struct.pack("<3f", *n) for n in normals)
            nv = self.add_view(nb, 34962)
            attrs["NORMAL"] = self.add_accessor(nv, 5126, len(normals), "VEC3")
        if uvs:
            ub = b"".join(struct.pack("<2f", u, 1.0 - v) for (u, v) in uvs)
            uv = self.add_view(ub, 34962)
            attrs["TEXCOORD_0"] = self.add_accessor(uv, 5126, len(uvs), "VEC2")
        fmt_char = "H" if elem == 2 else "I"
        ib = struct.pack("<%d%s" % (len(indices), fmt_char), *indices)
        iv = self.add_view(ib, 34963)
        iacc = self.add_accessor(iv, 5123 if elem == 2 else 5125, len(indices), "SCALAR")
        prim = {"attributes": attrs, "indices": iacc, "material": mat_idx}
        self.meshes.append({"name": name or "mesh", "primitives": [prim]})
        return len(self.meshes) - 1

    def add_node(self, name, matrix=None, mesh=None, children=None):
        node = {"name": name or "node"}
        if matrix:
            node["matrix"] = matrix
        if mesh is not None:
            node["mesh"] = mesh
        if children:
            node["children"] = children
        self.nodes.append(node)
        return len(self.nodes) - 1

    def write(self, path, scene_name):
        gltf = {
            "asset": {"version": "2.0", "generator": "rt2-scn2gltf"},
            "scene": 0,
            "scenes": [{"name": scene_name, "nodes": [self._root_index if hasattr(self, "_root_index") else 0]}],
            "nodes": self.nodes,
            "meshes": self.meshes,
            "materials": self.materials,
            "accessors": self.accessors,
            "bufferViews": self.bufferViews,
            "buffers": [{"uri": "buffer.bin", "byteLength": len(self.bin)}],
        }
        if self.images:
            gltf["images"] = [{"uri": img} for img in self.images]
            gltf["textures"] = self.textures
            gltf["samplers"] = [{"magFilter": 9729, "minFilter": 9987, "wrapS": 10497, "wrapT": 10497}]
        os.makedirs(os.path.dirname(path), exist_ok=True)
        with open(path, "w") as f:
            json.dump(gltf, f)
        with open(os.path.join(os.path.dirname(path), "buffer.bin"), "wb") as f:
            f.write(self.bin)
        return gltf


def build_node(builder, obj, parent_name=""):
    name = obj.get("name") or obj.get("tag")
    children = []
    mesh_idx = None
    shape = obj.get("mesh")
    if obj.get("tag") == "SGPS" and isinstance(shape, dict):
        if shape.get("tag") == "#RES":
            shape = shape.get("_next")
        if isinstance(shape, dict) and shape.get("tag") in ("MESH", "LICM") and shape.get("geometry"):
            mesh_idx = builder.mesh_from_geometry(shape["geometry"], name or "shape")
    for c in obj.get("children", []):
        ci = build_node(builder, c, name)
        if ci is not None:
            children.append(ci)
    mat = fixup_matrix(obj["matrix"]) if obj.get("matrix") else None
    if mesh_idx is None and not children and not mat:
        return None
    return builder.add_node(name, mat, mesh_idx, children)


def convert(scn_path, out_dir):
    sp.LOG.clear()
    version, root, consumed, total = sp.parse_scene(scn_path)
    builder = Builder()
    ri = build_node(builder, root)
    if ri is None:
        print("empty scene", scn_path)
        return
    builder._root_index = ri
    name = os.path.splitext(os.path.basename(scn_path))[0]
    gltf = builder.write(os.path.join(out_dir, name + ".gltf"), name)
    print(f"{name}: nodes={len(gltf['nodes'])} meshes={len(gltf['meshes'])} materials={len(gltf['materials'])} images={len(gltf.get('images', []))} bin={len(builder.bin)//1024}KB consumed={consumed}/{total}", flush=True)
    if sp.LOG:
        for l in sp.LOG[:4]:
            print("   !", l)


if __name__ == "__main__":
    targets = sys.argv[1:] if len(sys.argv) > 1 else [
        os.path.join(ASSETS, "data", "track", "Track_Tropical_1.scn"),
        os.path.join(ASSETS, "data", "track", "Track_Snow_1.scn"),
        os.path.join(ASSETS, "data", "car", "00", "car.pxc"),
        os.path.join(ASSETS, "data", "obj", "pickups.pxc"),
        os.path.join(ASSETS, "data", "scene", "scene_087a6493.scn"),
    ]
    for t in targets:
        try:
            convert(t, os.path.join(OUT, os.path.splitext(os.path.basename(t))[0]))
        except Exception as e:
            import traceback
            traceback.print_exc()
            print("FAILED", t, e, flush=True)
