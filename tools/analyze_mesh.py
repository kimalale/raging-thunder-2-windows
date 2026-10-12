import struct

FIXED = 65536.0
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


def pstring(data, p):
    n = struct.unpack_from("<H", data, p)[0]
    return data[p + 2:p + 2 + n].decode("latin1", "ignore").rstrip("\x00"), p + 2 + n


def walk(data, pos, end, depth=0, maxdepth=4):
    while pos + 8 <= end:
        tag = data[pos:pos + 4]
        size = struct.unpack_from("<I", data, pos + 4)[0]
        p = pos + 8
        pend = p + size
        if pend > end:
            print("  " * depth + f"@{pos} {tag!r} size={size} OVERRUN")
            return
        info = ""
        if tag in (b"SGGR", b"SGNO", b"SGPS", b"SGSP", b"SGME"):
            name, q = pstring(data, p)
            info = f"name={name!r}"
        if tag == b"TEXT":
            name, q = pstring(data, p)
            flags = struct.unpack_from("<I", data, q)[0]
            path, q2 = pstring(data, q + 4)
            info = f"name={name!r} flags={flags:#x} path={path!r}"
        print("  " * depth + f"@{pos} {tag.decode('latin1')} size={size} {info}")
        if tag in (b"SGGR", b"SGNO") and depth < maxdepth:
            name, q = pstring(data, p)
            q += 4  # flags
            n = struct.unpack_from("<I", data, q)[0]
            if n < 500:
                walk(data, q + 4, pend, depth + 1, maxdepth)
        pos = pend


def decode_mesh(data, off, size, label):
    end = off + size
    p = off
    real = struct.unpack_from("<i", data, p)[0] / FIXED
    p += 4
    bmin = struct.unpack_from("<3i", data, p)
    p += 12
    bmax = struct.unpack_from("<3i", data, p)
    p += 12
    vb_fmt, vb_count, vb_a, vb_b = struct.unpack_from("<IIHH", data, p)
    p += 12
    stride = calc_stride(vb_fmt)
    vb_bytes = stride * vb_count
    p += vb_bytes
    ib_type, ib_count, ib_a, ib_b = struct.unpack_from("<IIHH", data, p)
    p += 12
    elem = INDEX_ELEM[(ib_type - 1) & 7] if 1 <= ib_type <= 8 else 0
    ib_bytes = elem * ib_count
    p += ib_bytes
    print(f"--- {label} size={size}")
    print(f"    real={real:.3f} bmin={[round(v/65536,1) for v in bmin]} bmax={[round(v/65536,1) for v in bmax]}")
    print(f"    vb: fmt={vb_fmt:#x} count={vb_count} stride={stride} bytes={vb_bytes} a={vb_a} b={vb_b}")
    print(f"    ib: type={ib_type} count={ib_count} elem={elem} bytes={ib_bytes} a={ib_a} b={ib_b}")
    print(f"    consumed={p-off} remaining={end-p}")
    if end - p >= 4:
        n = struct.unpack_from("<I", data, p)[0]
        print(f"    next u32 (matCount?)={n} p={p}")
    return p


for path, label in [
    (r"C:\Users\Kim Malale\Desktop\workdesk\raging thunder\extracted_clean\data\obj\pickups.pxc", "pickups"),
    (r"C:\Users\Kim Malale\Desktop\workdesk\raging thunder\extracted_clean\data\car\00\car.pxc", "car00"),
]:
    b = open(path, "rb").read()
    print("=====", label)
    print("top objects:")
    walk(b, 8, len(b), 0, 1)
    i = b.find(b"MESH")
    if i >= 0:
        size = struct.unpack_from("<I", b, i + 4)[0]
        decode_mesh(b, i + 8, size, f"{label} first MESH @{i}")
    j = b.find(b"SGME")
    if j >= 0:
        size = struct.unpack_from("<I", b, j + 4)[0]
        name, q = pstring(b, j + 8)
        flags = struct.unpack_from("<I", b, q)[0]
        print(f"SGME @{j} size={size} name={name!r} flags={flags:#x}")
        decode_mesh(b, q + 4, size - (q + 4 - (j + 8)), f"{label} SGME-as-mesh")
