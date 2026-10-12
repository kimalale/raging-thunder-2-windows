import struct, os, sys, json, re

ROOT = r"C:\Users\Kim Malale\Desktop\workdesk\raging thunder"
VFS = os.path.join(ROOT, r"apk\assets\Data.vfs")
OUT = os.path.join(ROOT, "extracted")
STR_FILES = [os.path.join(ROOT, r"work\so_x86_strings.txt")]
DEX = os.path.join(ROOT, r"apk\classes.dex")

def pvfs_hash(s):
    h = 0
    i = 0
    n = len(s)
    if i < n and s[i] in "/\\":
        i += 1
    while i < n:
        c = s[i]
        i += 1
        if c == '\\':
            ch = 0x2f
        elif c == '/':
            if i < n and s[i] == '/':
                continue
            ch = 0x2f
        elif 'a' <= c <= 'z':
            ch = ord(c) - 0x20
        else:
            ch = ord(c)
        h = (h * 0x43 + ch - 0x71) & 0xFFFFFFFF
    return h

data = open(VFS, "rb").read()
magic, seed, count, dataoff = struct.unpack_from("<4sIII", data, 0)
entries = []
for i in range(count):
    h, size, endoff = struct.unpack_from("<III", data, 16 + i * 12)
    entries.append({"hash": h, "size": size, "start": endoff - size, "end": endoff})

# --- name cracking ---
strings = set()
for sf in STR_FILES:
    for line in open(sf, "r", encoding="utf-8", errors="ignore"):
        s = line.rstrip("\r\n")
        if 3 <= len(s) <= 80 and re.match(r"^[\w\-. /\\%]+$", s):
            strings.add(s)
# dex printable strings
b = open(DEX, "rb").read()
for m in re.finditer(rb"[\x20-\x7e]{4,80}", b):
    s = m.group().decode("ascii")
    if re.match(r"^[\w\-. /\\%]+$", s):
        strings.add(s)

prefixes = ["", "data/", "Data/", "data/audio/", "data/Audio/", "data/car/", "data/tex/", "data/menu/",
            "Data/Menu/", "data/fnt/", "data/icons/", "data/icons2/", "data/track/", "data/tracks/",
            "data/models/", "data/levels/", "data/hud/", "data/shaders/", "data/tex/Env/"]
known = {}
remaining = {e["hash"] for e in entries}
for s in strings:
    for base in (s, s.lower()):
        for p in prefixes:
            cand = p + base
            h = pvfs_hash(cand)
            if h in remaining:
                known[h] = cand
                remaining.discard(h)
                break

print("named:", len(known), "unknown:", len(remaining))

# --- extraction ---
os.makedirs(OUT, exist_ok=True)
summary = {}
for e in entries:
    h = e["hash"]
    name = known.get(h)
    if name:
        rel = name.replace("\\", "/").lstrip("/")
        dest = os.path.join(OUT, "Data", *rel.split("/"))
    else:
        dest = os.path.join(OUT, "unknown", f"{h:08x}.bin")
    os.makedirs(os.path.dirname(dest), exist_ok=True)
    with open(dest, "wb") as f:
        f.write(data[e["start"]:e["end"]])
    ext = os.path.splitext(dest)[1].lower() or "(none)"
    summary[ext] = summary.get(ext, 0) + 1

print("extracted by extension:")
for k in sorted(summary, key=summary.get, reverse=True):
    print(f"  {k}: {summary[k]}")

# unknown magic census
magics = {}
for e in entries:
    if e["hash"] in known:
        continue
    blob = data[e["start"]:e["start"] + 16]
    sig = blob[:4]
    pretty = sig.hex()
    if blob[:8] == b"\x89PNG\r\n\x1a\n":
        pretty = "PNG"
    elif blob[:4] == b"OggS":
        pretty = "OGG"
    elif blob[:4] == b"RIFF":
        pretty = "RIFF"
    elif blob[:4] == b"PVR\x03":
        pretty = "PVR3"
    elif blob[:1] == b"\x34":
        pretty = "PVR?"
    magics[pretty] = magics.get(pretty, 0) + 1
print("unknown magics:", dict(sorted(magics.items(), key=lambda x: -x[1])))

json.dump({f"{h:08x}": n for h, n in known.items()}, open(os.path.join(ROOT, "work", "cracked_names.json"), "w"), indent=0)
print("done ->", OUT)
