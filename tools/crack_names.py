import struct, os, re, json, zlib

ROOT = r"C:\Users\Kim Malale\Desktop\workdesk\raging thunder"
VFS = os.path.join(ROOT, r"apk\assets\Data.vfs")
NAMES_JSON = os.path.join(ROOT, "work", "cracked_names.json")
SO_STRINGS = os.path.join(ROOT, r"work\so_x86_strings.txt")
DEX = os.path.join(ROOT, r"apk\classes.dex")

def pvfs_hash(s):
    h = 0; i = 0; n = len(s)
    if i < n and s[i] in "/\\": i += 1
    while i < n:
        c = s[i]; i += 1
        if c == '\\': ch = 0x2f
        elif c == '/':
            if i < n and s[i] == '/': continue
            ch = 0x2f
        elif 'a' <= c <= 'z': ch = ord(c) - 0x20
        else: ch = ord(c)
        h = (h * 0x43 + ch - 0x71) & 0xFFFFFFFF
    return h

def unwrap(blob):
    if blob[:4] == b"PLZP" and len(blob) >= 12:
        usize, csize = struct.unpack_from("<II", blob, 4)
        for attempt in range(3):
            try:
                return zlib.decompress(blob[12:12 + csize])
            except Exception:
                break
    return blob

data = open(VFS, "rb").read()
magic, seed, count, dataoff = struct.unpack_from("<4sIII", data, 0)
entries = []
for i in range(count):
    h, size, endoff = struct.unpack_from("<III", data, 16 + i * 12)
    entries.append((h, size, endoff))

known = {}
for k, v in json.load(open(NAMES_JSON)).items():
    known[int(k, 16)] = v
remaining = {h for h, _, _ in entries} - set(known)

# payload cache
payloads = {}
for h, size, endoff in entries:
    payloads[h] = unwrap(data[endoff - size:endoff])

# base strings
strings = set()
for sf in (SO_STRINGS,):
    for line in open(sf, "r", encoding="utf-8", errors="ignore"):
        s = line.rstrip("\r\n")
        if 2 <= len(s) <= 90 and re.match(r"^[\w\-. /\\%()]+$", s):
            strings.add(s)
b = open(DEX, "rb").read()
for m in re.finditer(rb"[\x20-\x7e]{4,90}", b):
    s = m.group().decode("ascii")
    if re.match(r"^[\w\-. /\\%()]+$", s):
        strings.add(s)
for h in remaining:
    for m in re.finditer(rb"[\x20-\x7e]{3,90}", payloads[h][:200000]):
        s = m.group().decode("ascii")
        if re.match(r"^[\w\-. /\\%()]+$", s):
            strings.add(s)

dirs = ["", "data/", "Data/", "data/tex/", "data/Tex/", "data/tex/env/", "data/tex/Env/", "data/menu/", "Data/Menu/",
        "data/audio/", "data/Audio/", "data/fnt/", "data/icons/", "data/icons2/", "data/locale/", "data/loc/",
        "data/track/", "data/tracks/", "data/obj/", "data/objects/", "data/car/", "data/car/tex/", "data/models/",
        "data/scenes/", "data/levels/", "data/particles/", "data/effects/", "data/hud/", "data/gui/", "data/ui/",
        "data/sky/", "data/env/", "data/environment/", "data/terrain/", "data/world/", "data/shaders/", "data/materials/"]

exts = [".dds", ".pvr", ".plt", ".pxc", ".psn", ".glp", ".fnt", ".png", ".txt", ".bin", ".tex", ".mesh", ".xml", ".eng"]

def bump(cand):
    h = pvfs_hash(cand)
    if h in remaining:
        known[h] = cand
        remaining.discard(h)
        return True
    return False

added = 0
for s in strings:
    bases = {s}
    base, ext = os.path.splitext(s)
    if base and len(base) >= 2:
        for e in exts:
            bases.add(base + e)
    for b2 in list(bases):
        for d in dirs:
            if bump(d + b2):
                added += 1
print(f"string-based: +{added}")

# numeric patterns
patterns = []
for i in range(0, 100):
    patterns += [f"data/menu/mmap_{i}.pvr", f"data/menu/mmap_{i}.dds", f"data/menu/mmap{i}.pvr",
                 f"data/tex/Effect{i}.pvr", f"data/tex/Effect{i}.dds", f"data/tex/Effect{i:d}.pvr"]
    patterns += [f"data/tex/particle_{i:02d}.plt", f"data/tex/particle_{i:02d}.dds",
                 f"data/menu/rt2_{i:02d}.plt", f"data/menu/rt2_{i:02d}.dds",
                 f"data/menu/rt2_{i}.plt", f"data/icons2/icon_{i}.png"]
    for c in range(20):
        patterns += [f"data/car/{c:02d}/car.plt", f"data/car/{c:02d}/car.dds", f"data/car/{c:02d}/car.pxc",
                     f"data/car/{c:02d}/car.psn", f"data/car/{c:02d}/shadow.dds", f"data/car/{c:02d}/shadow.plt"]
padded = []
for i in range(0, 260):
    padded += [f"data/track/track{i:02d}.dds", f"data/tracks/track{i:02d}.dds"]
added2 = 0
for s in patterns + padded:
    if bump(s):
        added2 += 1
print(f"patterns: +{added2}")

print("total known:", len(known), "remaining:", len(remaining))
json.dump({f"{h:08x}": n for h, n in known.items()}, open(NAMES_JSON, "w"), indent=0)

# report remaining by magic
magic_count = {}
for h in remaining:
    p = payloads[h]
    sig = p[:4]
    if p[:8] == b"\x89PNG\r\n\x1a\n": k = "PNG"
    elif sig == b"DDS ": k = "DDS"
    elif sig == b"FCOL": k = "FCOL"
    elif sig[:2] == b"\x78\x9c": k = "zlib"
    else: k = sig.hex()
    magic_count[k] = magic_count.get(k, 0) + 1
print("remaining magics:", dict(sorted(magic_count.items(), key=lambda x: -x[1])))
