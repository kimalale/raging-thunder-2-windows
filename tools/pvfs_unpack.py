import struct, os, re, json, zlib

ROOT = r"C:\Users\Kim Malale\Desktop\workdesk\raging thunder"
VFS = os.path.join(ROOT, r"apk\assets\Data.vfs")
OUT = os.path.join(ROOT, "extracted")
NAMES_JSON = os.path.join(ROOT, "work", "cracked_names.json")

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

def unwrap(blob):
    if blob[:4] == b"PLZP" and len(blob) >= 12:
        usize, csize = struct.unpack_from("<II", blob, 4)
        try:
            payload = zlib.decompress(blob[12:12 + csize])
            return payload, "zlib"
        except Exception as ex:
            return blob, f"zlib-fail:{ex}"
    return blob, "raw"

def guess_ext(blob):
    if blob[:8] == b"\x89PNG\r\n\x1a\n": return ".png"
    if blob[:3] == b"\xff\xd8\xff": return ".jpg"
    if blob[:4] == b"OggS": return ".ogg"
    if blob[:4] == b"RIFF": return ".wav"
    if blob[:4] == b"\x50\x56\x52\x03": return ".pvr"
    if blob[:4] == b"\x34\x12\x03\x00": return ".pvr"
    if blob[:5] == b"<?xml": return ".xml"
    if blob[:1] == b"{": return ".json"
    if re.match(rb"^[\x20-\x7e\r\n\t]{40,}", blob[:200]): return ".txt"
    return ".bin"

data = open(VFS, "rb").read()
magic, seed, count, dataoff = struct.unpack_from("<4sIII", data, 0)
entries = []
for i in range(count):
    h, size, endoff = struct.unpack_from("<III", data, 16 + i * 12)
    entries.append({"hash": h, "size": size, "start": endoff - size, "end": endoff})

known = {}
for k, v in json.load(open(NAMES_JSON)).items():
    known[int(k, 16)] = v

dirs = ["data/", "Data/", "data/audio/", "data/Audio/", "data/car/", "data/tex/", "data/menu/", "Data/Menu/",
        "data/fnt/", "data/icons/", "data/icons2/", "data/track/", "data/tracks/", "data/models/",
        "data/levels/", "data/hud/", "data/shaders/", "data/tex/Env/", "data/tex/env/", "data/effects/",
        "data/particles/", "data/vehicles/", "data/scenes/", "data/gui/", "data/ui/"]

# round 0: payloads
payloads = {}
for e in entries:
    blob = data[e["start"]:e["end"]]
    payload, how = unwrap(blob)
    payloads[e["hash"]] = payload

# iterative name cracking from payload content
for rounds in range(3):
    candidates = set()
    for h, payload in payloads.items():
        head = payload[:400000]
        for m in re.finditer(rb"[\x20-\x7e]{3,90}", head):
            s = m.group().decode("ascii")
            if re.match(r"^[\w\-. /\\%()]+$", s):
                candidates.add(s)
    added = 0
    for s in candidates:
        for base in (s, s.lower()):
            if len(base) < 3 or len(base) > 90:
                continue
            for d in dirs:
                cand = d + base
                h = pvfs_hash(cand)
                if h in payloads and h not in known:
                    known[h] = cand
                    added += 1
                    break
    print(f"round {rounds}: +{added} names (total {len(known)})")
    if added == 0:
        break

# write outputs
os.makedirs(os.path.join(OUT, "Data"), exist_ok=True)
os.makedirs(os.path.join(OUT, "unknown"), exist_ok=True)
summary = {}
for e in entries:
    h = e["hash"]
    payload = payloads[h]
    name = known.get(h)
    if name:
        rel = name.replace("\\", "/").lstrip("/")
        dest = os.path.join(OUT, "Data", *rel.split("/"))
    else:
        dest = os.path.join(OUT, "unknown", f"{h:08x}{guess_ext(payload)}")
    os.makedirs(os.path.dirname(dest), exist_ok=True)
    with open(dest, "wb") as f:
        f.write(payload)
    ext = os.path.splitext(dest)[1].lower() or "(none)"
    summary[ext] = summary.get(ext, 0) + 1

print("extracted:", len(entries), "named:", sum(1 for e in entries if e["hash"] in known))
print("by extension:", dict(sorted(summary.items(), key=lambda x: -x[1])))
json.dump({f"{h:08x}": n for h, n in known.items()}, open(NAMES_JSON, "w"), indent=0)
print("done")
