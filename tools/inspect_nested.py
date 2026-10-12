import struct, os, re, zlib, json

ROOT = r"C:\Users\Kim Malale\Desktop\workdesk\raging thunder"
VFS = os.path.join(ROOT, r"apk\assets\Data.vfs")
OUT = os.path.join(ROOT, "work", "nested")
os.makedirs(OUT, exist_ok=True)

def unwrap(blob):
    if blob[:4] == b"PLZP" and len(blob) >= 12:
        usize, csize = struct.unpack_from("<II", blob, 4)
        return zlib.decompress(blob[12:12 + csize])
    return blob

data = open(VFS, "rb").read()
magic, seed, count, dataoff = struct.unpack_from("<4sIII", data, 0)
known = {int(k, 16): v for k, v in json.load(open(os.path.join(ROOT, "work", "cracked_names.json"))).items()}

def show(name, blob, n=64):
    head = blob[:n]
    hexs = head.hex(' ')
    asc = ''.join(chr(b) if 32 <= b < 127 else '.' for b in head)
    print(f"--- {name} ({len(blob)} bytes)")
    print("   ", hexs)
    print("   ", asc)

empty_found = []
for i in range(count):
    h, size, endoff = struct.unpack_from("<III", data, 16 + i * 12)
    blob = unwrap(data[endoff - size:endoff])
    if blob[:2] == b"\x78\x9c":
        try:
            inner = zlib.decompress(blob)
        except Exception as ex:
            print(f"nested {h:08x}: decompress fail {ex}")
            continue
        nm = known.get(h, f"{h:08x}")
        fn = re.sub(r"[^\w\-.]", "_", nm) + ".inner"
        open(os.path.join(OUT, fn), "wb").write(inner)
        textile = re.match(rb"^[\x09\x0a\x0d\x20-\x7e]+$", inner[:200])
        print(f"nested zlib {h:08x} named={nm} -> inner {len(inner)} bytes text={bool(textile)}")
        show("  inner head", inner, 96)
        if len(inner) < 2000:
            print("   full:", inner)
    elif blob[:4] == b"FCOL":
        nm = known.get(h, f"{h:08x}")
        print(f"FCOL {h:08x} named={nm} size={len(blob)}")
        show("  head", blob, 48)
    elif blob[:8] == b"\x89PNG\r\n\x1a\n" and h not in known:
        print(f"PNG unknown {h:08x} size={len(blob)}")
    elif len(blob) <= 16:
        empty_found.append((h, len(blob), known.get(h)))
print("tiny/empty files:", empty_found)
