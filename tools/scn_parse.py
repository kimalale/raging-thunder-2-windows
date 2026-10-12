"""Raging Thunder 2 .scn/.pxc parser -> glTF 2.0 exporter.

Format recovered from librthunder2lite.so (see ../README.md):
  object := [4-char tag][u32 payloadSize][payload]
  CSGObject = PString name + u32 flags
  CSGGroup  = CSGObject + u32 childCount + children*
  CSGSpatial= CSGObject + real + vec3 + vec3 + matrix43 + dynamics-object
  CSGNode   = CSGGroup + real + vec3 + vec3 + matrix43 + dynamics-object
  CSGCamera = CSGSpatial + 4 * real
  CSGMeta   = CSGSpatial + u32 count + (PString + variant-object)*
  CSGPolyShape = CSGSpatial + mesh-object + 10 * real
  CPolyMesh = name + flags + real + vec3 + vec3 + vertexbuf + indexbuf
              + u32 matCount + materials + u32 count + count*8
  CVertexBuffer / CIndexBuffer = u32 + u32 + u16 + u16 + payload
"""
import struct, os, sys, json, math, shutil

ROOT = r"C:\Users\Kim Malale\Desktop\workdesk\raging thunder"
ASSETS = os.path.join(ROOT, "extracted_clean")
OUT = os.path.join(ROOT, "export")

FIXED = 65536.0


def fx(v):
    x = struct.unpack_from("<i", v)[0]
    return x / FIXED


class Reader:
    def __init__(self, data, pos=0, end=None, version=0x10013):
        self.d = data
        self.p = pos
        self.end = len(data) if end is None else end
        self.version = version

    def u32(self):
        v = struct.unpack_from("<I", self.d, self.p)[0]
        self.p += 4
        return v

    def i32(self):
        v = struct.unpack_from("<i", self.d, self.p)[0]
        self.p += 4
        return v

    def u16(self):
        v = struct.unpack_from("<H", self.d, self.p)[0]
        self.p += 2
        return v

    def raw(self, n):
        v = self.d[self.p:self.p + n]
        self.p += n
        return v

    def real(self):
        v = struct.unpack_from("<i", self.d, self.p)[0] / FIXED
        self.p += 4
        return v

    def vec3(self):
        return (self.real(), self.real(), self.real())

    def mat43(self):
        return [self.real() for _ in range(12)]

    def pstring(self):
        n = self.u16()
        s = self.raw(n)
        try:
            t = s.decode("ascii").rstrip("\x00")
        except Exception:
            t = s.decode("latin1").rstrip("\x00")
        return t

    def timeline(self):
        active = self.u32() if False else self.raw(1)[0] != 0
        value = self.real()
        speed = self.real()
        limit = self.real()
        mode = self.u32()
        return {"active": active, "value": value, "speed": speed,
                "limit": limit, "mode": mode}


def parse_variant(r, tag):
    if tag == b"REAL":
        return r.real()
    if tag == b"UI32":
        return r.u32()
    if tag in (b"STRI", b"TEXT"):
        return r.pstring()
    if tag == b"UI16":
        return r.u16()
    return None


def read_object(r, depth=0, log=None):
    """Returns dict describing the framed object, or None at end."""
    if depth > 64 or r.p + 8 > r.end:
        return None
    start = r.p
    tag = r.raw(4)
    size = r.u32()
    pstart = r.p
    pend = pstart + size
    if pend > r.end:
        raise ValueError(f"object {tag} size {size} overruns at {start}")
    obj = {"tag": tag.decode("latin1"), "name": None, "children": [], "mesh": None,
           "matrix": None, "params": []}
    try:
        if tag == b"SANI":
            sub = Reader(r.d, pstart, pend, r.version)
            flags = sub.u32()
            obj["flags"] = flags
            tls = []
            for bit in (1, 2, 4, 8):
                if flags & bit:
                    t = sub.timeline()
                    t["bit"] = bit
                    tls.append(t)
            obj["timelines"] = tls
            if flags & 8:
                n = min(sub.u32(), 100000)
                obj["waypoints"] = [sub.vec3() for _ in range(n)]
        elif tag in (b"SGGR", b"SGNO", b"SGSP", b"SGPS", b"SGCA", b"SGCO", b"SGCU",
                     b"SGME", b"SGPC"):
            sub = Reader(r.d, pstart, pend, r.version)
            obj["name"] = sub.pstring()
            obj["flags"] = sub.u32()
            if tag == b"SGGR":
                n = min(sub.u32(), 100000)
                for _ in range(n):
                    before = sub.p
                    child = read_object(sub, depth + 1, log)
                    if child is None or sub.p == before:
                        break
                    obj["children"].append(child)
            elif tag == b"SGNO":
                n = min(sub.u32(), 100000)
                for _ in range(n):
                    before = sub.p
                    child = read_object(sub, depth + 1, log)
                    if child is None or sub.p == before:
                        break
                    obj["children"].append(child)
                obj["scale"] = sub.real()
                obj["pos"] = sub.vec3()
                obj["rot"] = sub.vec3()
                obj["matrix"] = sub.mat43()
                obj["dynamics"] = read_object(sub, depth + 1, log)
            elif tag == b"SGSP":
                obj["scale"] = sub.real()
                obj["pos"] = sub.vec3()
                obj["rot"] = sub.vec3()
                obj["matrix"] = sub.mat43()
                obj["dynamics"] = read_object(sub, depth + 1, log)
            elif tag == b"SGCA":
                obj["scale"] = sub.real()
                obj["pos"] = sub.vec3()
                obj["rot"] = sub.vec3()
                obj["matrix"] = sub.mat43()
                obj["dynamics"] = read_object(sub, depth + 1, log)
                obj["camera"] = [sub.real() for _ in range(4)]
            elif tag == b"SGPS":
                obj["scale"] = sub.real()
                obj["pos"] = sub.vec3()
                obj["rot"] = sub.vec3()
                obj["matrix"] = sub.mat43()
                obj["dynamics"] = read_object(sub, depth + 1, log)
                obj["mesh"] = read_object(sub, depth + 1, log)
                obj["shape_vals"] = [sub.real() for _ in range(9)]
                if r.version >= 0x10011 and sub.p + 4 <= pend:
                    obj["shape_val_extra"] = sub.real()
            elif tag == b"SGME":
                pass
            elif tag == b"SGPC":
                n = min(sub.u32(), 100000)
                for _ in range(n):
                    before = sub.p
                    child = read_object(sub, depth + 1, log)
                    if child is None or sub.p == before:
                        break
                    obj["children"].append(child)
            elif tag == b"SGCU":
                n = min(sub.u32(), 100000)
                pts = []
                for _ in range(n):
                    pts.append(sub.vec3())
                obj["points"] = pts
                scn = min(sub.u32(), 100000)
                obj["subcurves"] = scn
                for _ in range(scn):
                    m = min(sub.u32(), 100000)
                    for _ in range(m):
                        sub.vec3()
                    sub.real()
                    sub.real()
            elif tag == b"SGCO":
                pass
            elif tag == b"SGCU":
                pass
            elif tag == b"LICM":
                pass
            else:
                pass
        elif tag == b"TEXT":
            sub = Reader(r.d, pstart, pend, r.version)
            obj["flags"] = sub.u32()
            obj["path"] = sub.pstring()
        elif tag == b"#RES":
            nxt = r.d[r.p:r.p + 4]
            if len(nxt) == 4 and all((65 <= ch <= 90) or (48 <= ch <= 57) for ch in nxt):
                obj["_next"] = read_object(r, depth + 1, log)
        elif tag == b"MESH":
            sub = Reader(r.d, pstart, pend, r.version)
            obj["geometry"] = read_geometry(sub, log)
        elif tag == b"LICM":
            sub = Reader(r.d, pstart, pend, r.version)
            obj["geometry"] = read_geometry(sub, log)
        elif tag in (b"REAL", b"UI32", b"STRI", b"UI16"):
            sub = Reader(r.d, pstart, pend, r.version)
            obj["value"] = parse_variant(sub, tag)
        elif tag in (b"MESH", b"#RES", b"DYNA", b"SKEL", b"NODE", b"ANIM"):
            sub = Reader(r.d, pstart, pend, r.version)
            obj["name"] = sub.pstring() if tag == b"MESH" else None
            obj["payload"] = r.d[pstart:pend]
        else:
            obj["payload"] = r.d[pstart:pend]
    except Exception as ex:
        if log is not None:
            log.append(f"parse error in {tag} @{start}: {ex}")
        obj["error"] = str(ex)
    if log is not None and r.p != pend:
        log.append(f"tag {tag.decode('latin1')} @{start} consumed {r.p - pstart} of {size}")
    r.p = pend
    return obj


INDEX_ELEM = [1, 1, 2, 2, 4, 4, 4, 4]


def calc_stride(fmt):
    base = 12 if (fmt & 3) == 0 else 8
    if fmt & 0x30:
        base += 12
    if (fmt & 0xf00) == 0x100:
        base += 4
    if fmt & 0x3000:
        base += 8
    return base


def decode_vertices(blob, fmt, count, log):
    stride = calc_stride(fmt)
    if stride <= 0 or stride * count != len(blob):
        log.append(f"vertex size mismatch: stride={stride} count={count} bytes={len(blob)}")
        return [], [], []
    pos_ch = 2 if (fmt & 0xf) in (2, 3) else 3
    pos_float = (fmt & 0xf) in (1, 3)
    norm_mode = fmt & 0xf0
    uv_mode = fmt & 0x3000
    pos_off = 0
    norm_off = (12 if (fmt & 3) == 0 else 8) if norm_mode else None
    color_off = None
    uv_off = None
    off = (12 if (fmt & 3) == 0 else 8)
    if norm_mode:
        off += 12
    if (fmt & 0xf00) == 0x100:
        color_off = off
        off += 4
    if uv_mode:
        uv_off = off
        off += 8
    positions, normals, uvs = [], [], []
    for i in range(count):
        base = i * stride
        if pos_float:
            px, py, pz = struct.unpack_from("<3f", blob, base + pos_off)
        else:
            px, py, pz = (struct.unpack_from("<i", blob, base + pos_off + 4 * k)[0] / FIXED for k in range(3))
        positions.append((px, py, pz))
        if norm_off is not None:
            if norm_mode == 0x20:
                nx, ny, nz = struct.unpack_from("<3f", blob, base + norm_off)
            else:
                nx, ny, nz = (struct.unpack_from("<i", blob, base + norm_off + 4 * k)[0] / FIXED for k in range(3))
            normals.append((nx, ny, nz))
        if uv_off is not None:
            if uv_mode == 0x2000:
                u, v = struct.unpack_from("<2f", blob, base + uv_off)
            else:
                u, v = (struct.unpack_from("<i", blob, base + uv_off + 4 * k)[0] / FIXED for k in range(2))
            uvs.append((u, v))
    return positions, normals, uvs


def read_geometry(r, log):
    """CPolyMesh payload after name+flags+real+vec3+vec3 at current position."""
    try:
        r.real()
        r.vec3()
        r.vec3()
        # vertex buffer (inline)
        vb_fmt = r.u32()
        vb_count = r.u32()
        vb_a = r.u16()
        vb_b = r.u16()
        vb_stride = calc_stride(vb_fmt)
        vb_data = r.raw(vb_stride * vb_count)
        # index buffer (inline)
        ib_type = r.u32()
        ib_count = r.u32()
        ib_a = r.u16()
        ib_b = r.u16()
        elem = INDEX_ELEM[(ib_type - 1) & 7] if 1 <= ib_type <= 8 else 2
        ib_data = r.raw(elem * ib_count)
        geo = {"vb_fmt": vb_fmt, "vb_count": vb_count, "vb_data": vb_data,
               "ib_type": ib_type, "ib_count": ib_count, "ib_elem": elem, "ib_data": ib_data,
               "materials": []}
        # materials (engine layout: framed texture object + fixed-size material fields)
        mat_count = r.u32()
        if mat_count > 100000:
            log.append(f"material count insane: {mat_count}")
            return geo
        mat_start = r.p
        best = None
        for field_size in (48, 40):
            try:
                r.p = mat_start
                mats = []
                ok = True
                for _ in range(mat_count):
                    tobj = read_object(r, 0, log)
                    if r.p + field_size > r.end:
                        ok = False
                        break
                    fields = r.raw(field_size)
                    tex_path = None
                    if isinstance(tobj, dict):
                        t = tobj
                        if t.get("tag") == "#RES":
                            t = t.get("_next") or {}
                        tex_path = t.get("path")
                    shader_id = struct.unpack_from("<I", fields, 20)[0] if len(fields) >= 24 else None
                    flags = struct.unpack_from("<H", fields, 2)[0] if len(fields) >= 4 else 0
                    color = struct.unpack_from("<I", fields, 4)[0] if len(fields) >= 8 else 0
                    mode = struct.unpack_from("<I", fields, 24)[0] if len(fields) >= 28 else 0
                    alpha_ref = struct.unpack_from("<i", fields, 44)[0] / 65536.0 if len(fields) >= 48 else 0.0
                    reals = []
                    for off in (28, 32, 36, 40):
                        if len(fields) >= off + 4:
                            reals.append(struct.unpack_from("<i", fields, off)[0] / 65536.0)
                    uv_reals = []
                    tail32 = struct.unpack_from("<I", fields, 44)[0] if len(fields) >= 48 else 0
                    mats.append({"tag": "MAT", "path": tex_path, "shader": shader_id,
                                 "flags": flags, "color": color, "mode": mode,
                                 "alpha_ref": alpha_ref, "reals": reals, "uv_reals": uv_reals,
                                 "tail32": tail32, "rawhex": fields.hex()})
                if not ok or r.p + 4 > r.end:
                    continue
                n = struct.unpack_from("<I", r.d, r.p)[0]
                remaining = r.end - (r.p + 4)
                if n >= 100000 or n * 8 > remaining:
                    continue
                draws = []
                for i in range(n):
                    bv, mi, ic, ist = struct.unpack_from("<4H", r.d, r.p + 4 + i * 8)
                    draws.append({"baseVertex": bv, "material": mi, "indexCount": ic, "indexStart": ist})
                if any(d["material"] >= max(mat_count, 1) for d in draws):
                    continue
                exact = remaining == n * 8
                cand = (exact, field_size, mats, n, draws, r.p + 4 + n * 8)
                if best is None or (exact and not best[0]):
                    best = cand
                if exact:
                    break
            except Exception:
                continue
        if best is None:
            log.append(f"material block unresolved (matCount={mat_count})")
            r.p = r.end
            geo["materials"] = []
            geo["draws"] = []
            geo["trailer"] = None
            return geo
        exact, field_size, mats, n, draws, endpos = best
        geo["materials"] = mats
        geo["draws"] = draws
        geo["trailer"] = field_size
        geo["draws_exact"] = exact
        geo["tail_count"] = 0
        geo["tail"] = b""
        r.p = r.end
        return geo
    except Exception as ex:
        log.append(f"geometry error: {ex}")
        return None


def parse_scene(path):
    b = open(path, "rb").read()
    assert b[:4] == b"\x4d\x3c\x2b\x1a", "bad magic"
    version = struct.unpack_from("<I", b, 4)[0]
    r = Reader(b, 8, len(b), version)
    root = read_object(r, 0, LOG)
    return version, root, r.p, len(b)


LOG = []


def count_nodes(o):
    if o is None:
        return 0
    return 1 + sum(count_nodes(c) for c in o.get("children", []))


def count_meshes(o):
    if o is None:
        return 0
    n = 1 if (o.get("tag") == "MESH" and o.get("geometry")) else 0
    m = o.get("mesh")
    if isinstance(m, dict):
        if m.get("tag") == "#RES":
            m = m.get("_next")
        if isinstance(m, dict) and m.get("tag") == "MESH" and m.get("geometry"):
            n += 1
    return n + sum(count_meshes(c) for c in o.get("children", []))


if __name__ == "__main__":
    targets = []
    for dirpath, dirs, files in os.walk(ASSETS):
        for f in files:
            if f.lower().endswith((".scn", ".pxc")):
                targets.append(os.path.join(dirpath, f))
    for path in sorted(targets):
        LOG.clear()
        try:
            version, root, consumed, total = parse_scene(path)
            nam = os.path.basename(path)
            print(f"{nam}: ver=0x{version:x} nodes={count_nodes(root)} meshes={count_meshes(root)} consumed={consumed}/{total}", flush=True)
            if LOG:
                for l in LOG[:6]:
                    print("   !", l, flush=True)
        except Exception as e:
            print(f"{os.path.basename(path)}: FAILED {e}", flush=True)
