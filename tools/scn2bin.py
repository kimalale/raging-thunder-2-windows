"""Export RT2 scenes to a runtime-loadable binary format with full vertex data
(positions, normals, uvs, COLORS) + per-draw materials, for the C# runtime loader.

Layout (little endian):
  scene.json          manifest (nodes with world matrices, meshes, materials, buffer offsets)
  scene.verts.bin     float32 arrays: pos xyz, nrm xyz (if any), uv xy (if any), rgba (if any)
  scene.idx.bin       uint32 indices per mesh (per-draw ranges)
"""
import os, sys, struct, json, shutil

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import scn_parse as sp
import scn2obj as obj

ROOT = r"C:\Users\Kim Malale\Desktop\workdesk\raging thunder"
ASSETS = os.path.join(ROOT, "extracted_clean")
OUT = os.path.join(ROOT, "export", "bin")


def export_scene(scn_path, skip=None):
    sp.LOG.clear()
    name = os.path.splitext(os.path.basename(scn_path))[0]
    out_dir = os.path.join(OUT, name)
    os.makedirs(out_dir, exist_ok=True)
    version, root, consumed, total = sp.parse_scene(scn_path)

    verts = bytearray()
    idxs = bytearray()
    meshes = []
    materials = []
    mat_by_key = {}
    nodes = []

    def material_id(tex, out_dir, shader_id=None, flags=0, mode=0, alpha_ref=0.0, reals=None):
        key = (tex or "__default__", shader_id, flags, mode)
        if key in mat_by_key:
            return mat_by_key[key]
        png = None
        if tex:
            png = obj.find_texture(tex)
            if png:
                stem = os.path.splitext(os.path.basename(png))[0] + ".png"
                dest = os.path.join(out_dir, stem)
                if not os.path.isfile(dest):
                    try:
                        from PIL import Image
                        Image.open(png).convert("RGBA").save(dest)
                    except Exception:
                        pass
                png = stem if os.path.isfile(dest) else None
        reals = reals or []
        scroll_u = reals[0] if (flags & 0x80) and len(reals) > 0 else 0.0
        scroll_v = reals[1] if (flags & 0x80) and len(reals) > 1 else 0.0
        materials.append({"name": os.path.splitext(os.path.basename(tex or "default"))[0], "texture": png,
                          "shader": shader_id, "flags": flags, "mode": mode,
                          "alphaRef": round(alpha_ref, 4), "scrollU": round(scroll_u, 4), "scrollV": round(scroll_v, 4)})
        mat_by_key[key] = len(materials) - 1
        return mat_by_key[key]

    def add_geo(geo, world, node_name):
        positions, normals, uvs = sp.decode_vertices(geo["vb_data"], geo["vb_fmt"], geo["vb_count"], sp.LOG)
        if not positions:
            return None
        has_n = bool(normals)
        has_uv = bool(uvs)
        colors = decode_colors(geo)
        has_col = bool(colors)
        v_off = len(verts)
        for i, p in enumerate(positions):
            wp = obj.xform_point(world, p)
            verts.extend(struct.pack("<3f", *wp))
            if has_n:
                nn = obj.xform_dir(world, normals[i]) if i < len(normals) else (0.0, 1.0, 0.0)
                verts.extend(struct.pack("<3f", *nn))
            if has_uv:
                u, v = uvs[i] if i < len(uvs) else (0.0, 0.0)
                verts.extend(struct.pack("<2f", u, v))
            if has_col:
                r, g, b, a = colors[i] if i < len(colors) else (255, 255, 255, 255)
                verts.extend(struct.pack("<4B", r, g, b, a))
        elem = geo["ib_elem"] or 2
        if elem == 2:
            indices = list(struct.unpack_from("<%dH" % geo["ib_count"], geo["ib_data"]))
        elif elem == 4:
            indices = list(struct.unpack_from("<%dI" % geo["ib_count"], geo["ib_data"]))
        else:
            indices = list(geo["ib_data"][:geo["ib_count"]])
        i_off = len(idxs) // 4
        for ix in indices:
            idxs.extend(struct.pack("<I", ix))
        draws = geo.get("draws") or []
        mats = geo.get("materials") or []
        draw_list = []
        if draws:
            for d in draws:
                mi = d["material"] if d["material"] < len(mats) else 0
                mm2 = mats[mi] if mats else {}
                tex = mm2.get("path")
                draw_list.append({"start": d["indexStart"], "count": d["indexCount"],
                                  "material": material_id(tex, out_dir, mm2.get("shader"), mm2.get("flags", 0),
                                                          mm2.get("mode", 0), mm2.get("alpha_ref", 0.0),
                                                          mm2.get("reals"))})
        else:
            tex = None
            shader_id = None
            flags = 0
            for m in mats:
                if isinstance(m, dict) and m.get("path"):
                    tex = m["path"]
                    shader_id = m.get("shader")
                    flags = m.get("flags", 0)
                    break
            draw_list.append({"start": 0, "count": len(indices), "material": material_id(tex, out_dir, shader_id, flags)})
        mm = {"name": node_name, "voff": v_off, "vcount": len(positions),
              "has_n": has_n, "has_uv": has_uv, "has_col": has_col,
              "ioff": i_off, "icount": len(indices), "draws": draw_list,
              "min": [min(p[k] for p in positions) for k in range(3)],
              "max": [max(p[k] for p in positions) for k in range(3)]}
        meshes.append(mm)
        return len(meshes) - 1

    def inv44(m):
        r = [0.0] * 16
        a = ((m[0], m[1], m[2]), (m[4], m[5], m[6]), (m[8], m[9], m[10]))
        det = (a[0][0] * (a[1][1] * a[2][2] - a[1][2] * a[2][1])
               - a[0][1] * (a[1][0] * a[2][2] - a[1][2] * a[2][0])
               + a[0][2] * (a[1][0] * a[2][1] - a[1][1] * a[2][0]))
        if abs(det) < 1e-12:
            return None
        id_ = 1.0 / det
        inv = [
            [(a[1][1] * a[2][2] - a[1][2] * a[2][1]) * id_, (a[0][2] * a[2][1] - a[0][1] * a[2][2]) * id_, (a[0][1] * a[1][2] - a[0][2] * a[1][1]) * id_],
            [(a[1][2] * a[2][0] - a[1][0] * a[2][2]) * id_, (a[0][0] * a[2][2] - a[0][2] * a[2][0]) * id_, (a[0][2] * a[1][0] - a[0][0] * a[1][2]) * id_],
            [(a[1][0] * a[2][1] - a[1][1] * a[2][0]) * id_, (a[0][1] * a[2][0] - a[0][0] * a[2][1]) * id_, (a[0][0] * a[1][1] - a[0][1] * a[1][0]) * id_],
        ]
        t = (m[12], m[13], m[14])
        it = (-(inv[0][0] * t[0] + inv[0][1] * t[1] + inv[0][2] * t[2]),
              -(inv[1][0] * t[0] + inv[1][1] * t[1] + inv[1][2] * t[2]),
              -(inv[2][0] * t[0] + inv[2][1] * t[1] + inv[2][2] * t[2]))
        r = [inv[0][0], inv[0][1], inv[0][2], 0.0,
             inv[1][0], inv[1][1], inv[1][2], 0.0,
             inv[2][0], inv[2][1], inv[2][2], 0.0,
             it[0], it[1], it[2], 1.0]
        return r

    def walk(o, world, anim_world=None, anim_data=None):
        mat = o.get("matrix")
        if mat:
            world = obj.mat4_mul(world, obj.m43_to_m44(mat))
        nm = o.get("name") or o.get("tag") or "node"
        if skip is not None and skip(nm or ""):
            return
        dyn = o.get("dynamics")
        if isinstance(dyn, dict) and dyn.get("tag") == "SANI":
            anim_world = world
            anim_data = dyn
        mesh_world = world
        if anim_world is not None:
            iw = inv44(anim_world)
            if iw is not None:
                mesh_world = obj.mat4_mul(iw, world)

        def emit(mi):
            n = {"name": nm, "mesh": mi}
            if anim_world is not None and anim_data is not None:
                wps = anim_data.get("waypoints", [])
                flat = []
                for w in wps:
                    flat.extend([round(w[0], 4), round(w[1], 4), round(w[2], 4)])
                n["base"] = [round(v, 6) for v in anim_world]
                n["anim"] = {"flags": anim_data.get("flags", 0),
                             "timelines": anim_data.get("timelines", []),
                             "wp": flat}
            nodes.append(n)

        g = o.get("geometry")
        if isinstance(g, dict):
            mi = add_geo(g, mesh_world, nm)
            if mi is not None:
                emit(mi)
        shape = o.get("mesh")
        if o.get("tag") == "SGPS" and isinstance(shape, dict):
            if shape.get("tag") == "#RES":
                shape = shape.get("_next")
            if isinstance(shape, dict) and shape.get("tag") in ("MESH", "LICM") and shape.get("geometry"):
                mi = add_geo(shape["geometry"], mesh_world, nm)
                if mi is not None:
                    emit(mi)
        for c in o.get("children", []):
            walk(c, world, anim_world, anim_data)

    walk(root, [1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1])

    with open(os.path.join(out_dir, "scene.verts.bin"), "wb") as f:
        f.write(verts)
    with open(os.path.join(out_dir, "scene.idx.bin"), "wb") as f:
        f.write(idxs)
    manifest = {"version": 1, "name": name, "nodes": nodes, "meshes": meshes, "materials": materials}
    with open(os.path.join(out_dir, "scene.json"), "w") as f:
        json.dump(manifest, f)
    print(f"{name}: nodes={len(nodes)} meshes={len(meshes)} materials={len(materials)} "
          f"verts={len(verts)//4} floats idx={len(idxs)//4}", flush=True)


def decode_colors(geo):
    fmt = geo["vb_fmt"]
    if (fmt & 0xf00) != 0x100:
        return []
    stride = sp.calc_stride(fmt)
    off = (12 if (fmt & 3) == 0 else 8) + (12 if fmt & 0x30 else 0)
    n = geo["vb_count"]
    out = []
    for i in range(n):
        base = i * stride + off
        try:
            out.append(struct.unpack_from("<4B", geo["vb_data"], base))
        except Exception:
            out.append((255, 255, 255, 255))
    return out


if __name__ == "__main__":
    targets = sys.argv[1:]
    if not targets:
        targets = [
            os.path.join(ASSETS, r"data\track\Track_Tropical_1.scn"),
            os.path.join(ASSETS, r"data\car\00\car.pxc"),
        ]
    for t in targets:
        skip = obj.stock_variant_skip if t.lower().endswith("car.pxc") else None
        try:
            export_scene(t, skip)
        except Exception as e:
            import traceback
            traceback.print_exc()
            print("FAILED", t, e, flush=True)
