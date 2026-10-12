import struct, os, re, zlib, json, shutil

ROOT = r"C:\Users\Kim Malale\Desktop\workdesk\raging thunder"
VFS = os.path.join(ROOT, r"apk\assets\Data.vfs")
CLEAN = os.path.join(ROOT, "extracted_clean")
NAMES_JSON = os.path.join(ROOT, "work", "cracked_names.json")

BAD_NAMES = {"data/car/oX/P.pvr", "data/models/1ZU.bin", "data/terrain/vhA.fnt"}

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
        return zlib.decompress(blob[12:12 + csize])
    return blob

data = open(VFS, "rb").read()
magic, seed, count, dataoff = struct.unpack_from("<4sIII", data, 0)
known = {}
for k, v in json.load(open(NAMES_JSON)).items():
    nm = v.replace("\\", "/")
    if nm in BAD_NAMES:
        continue
    known[int(k, 16)] = nm

# harvest strings from all payload contents for extra name cracking
allpayload = {}
for i in range(count):
    h, size, endoff = struct.unpack_from("<III", data, 16 + i * 12)
    blob = unwrap(data[endoff - size:endoff])
    if blob[:2] == b"\x78\x9c":
        try:
            blob = zlib.decompress(blob)
        except Exception:
            pass
    allpayload[h] = blob

cands = set()
for h, blob in allpayload.items():
    for m in re.finditer(rb"[\x20-\x7e]{3,80}", blob[:600000]):
        s = m.group().decode("ascii")
        if re.match(r"^[\w\-. /\\%()]+$", s):
            cands.add(s)
remaining = set(allpayload) - set(known)
dirs = ["data/tex/", "data/Tex/", "data/track/", "data/track/tex/", "data/env/", "data/tex/env/", "data/menu/",
        "data/car/tex/", "data/fnt/", "Data/"]
exts = [".dds", ".plt", ".pvr", ".png", ".psn"]
added = 0
for s in cands:
    bases = {s}
    base, ext = os.path.splitext(s)
    if base and len(base) >= 2:
        for e in exts:
            bases.add(base + e)
    for b2 in list(bases):
        for d in dirs:
            hh = pvfs_hash(d + b2)
            if hh in remaining:
                known[hh] = d + b2
                remaining.discard(hh)
                added += 1
print("extra cracked:", added, "remaining:", len(remaining))

# classify and write clean tree
if os.path.exists(CLEAN):
    shutil.rmtree(CLEAN)
index = []
used_paths = {}
for i in range(count):
    ehash, size, endoff = struct.unpack_from("<III", data, 16 + i * 12)
    blob = allpayload[ehash]
    name = known.get(ehash)
    if blob[:4] == b"\x4d\x3c\x2b\x1a":  # scene
        m = re.search(rb"static:([\w]+)", blob)
        if name:
            rel = name if name.endswith(".pxc") else name + ".pxc"
        elif m:
            rel = f"data/track/{m.group(1).decode()}.scn"
        else:
            rel = f"data/scene/scene_{ehash:08x}.scn"
        kind = "scene"
    elif blob[:4] == b"DDS ":
        if name and name.startswith("data/"):
            rel = name if name.endswith(".dds") else name + ".dds"
        else:
            rel = f"data/unktex/{ehash:08x}.dds"
        kind = "dds"
    elif blob[:4] == b"FCOL":
        rel = name if name else f"data/strtab/{ehash:08x}.fcol"
        kind = "fcol"
    elif blob[:8] == b"\x89PNG\r\n\x1a\n":
        rel = name if name else f"data/img/{ehash:08x}.png"
        kind = "png"
    elif name:
        rel = name
        kind = "named"
    else:
        ext = ".bin"
        if blob[:4] == b"OggS": ext = ".ogg"
        elif blob[:4] == b"RIFF": ext = ".wav"
        rel = f"data/misc/{ehash:08x}{ext}"
        kind = "misc"
    key = rel.lower()
    if key in used_paths and used_paths[key] != ehash:
        root, ext = os.path.splitext(rel)
        rel = f"{root}_{ehash:08x}{ext}"
    used_paths[rel.lower()] = ehash
    dest = os.path.join(CLEAN, *rel.split("/"))
    os.makedirs(os.path.dirname(dest), exist_ok=True)
    with open(dest, "wb") as f:
        f.write(blob)
    index.append({"hash": f"{ehash:08x}", "kind": kind, "name": rel, "size": len(blob)})

json.dump(index, open(os.path.join(ROOT, "work", "asset_index.json"), "w"), indent=1)
kinds = {}
for it in index:
    kinds[it["kind"]] = kinds.get(it["kind"], 0) + 1
print("kinds:", kinds)
print("scenes:")
for it in index:
    if it["kind"] == "scene":
        print("  ", it["name"], it["size"])
