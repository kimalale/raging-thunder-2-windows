import struct, os, re, json

ROOT = r"C:\Users\Kim Malale\Desktop\workdesk\raging thunder"
VFS = os.path.join(ROOT, r"apk\assets\Data.vfs")
NAMES_JSON = os.path.join(ROOT, "work", "cracked_names.json")
SCENES = os.path.join(ROOT, "extracted_clean", "data")

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

data = open(VFS, "rb").read()
magic, seed, count, dataoff = struct.unpack_from("<4sIII", data, 0)
entry_hashes = set()
for i in range(count):
    h, size, endoff = struct.unpack_from("<III", data, 16 + i * 12)
    entry_hashes.add(h)

known = {int(k, 16): v for k, v in json.load(open(NAMES_JSON)).items()}
remaining = entry_hashes - set(known)

name_re = re.compile(rb"[A-Za-z0-9_][A-Za-z0-9_\\/.\-]{2,90}\.(?:pvr|dds|plt|png|psn|pxc|glp|fnt|txt|bin)")
raw = set()
for dirpath, dirs, files in os.walk(SCENES):
    for f in files:
        if f.lower().endswith((".scn", ".pxc")):
            b = open(os.path.join(dirpath, f), "rb").read()
            for m in name_re.finditer(b):
                raw.add(m.group().decode("ascii", "ignore"))
print("raw names:", len(raw))

EXTS = [".pvr", ".dds", ".plt", ".psn", ".png", ".pxc", ".glp", ".fnt"]
cands = set()
for s in raw:
    for sl in (s, s.replace("\\", "/"), s.replace("/", "\\")):
        base, ext = os.path.splitext(sl)
        variants = {sl}
        for e in EXTS:
            variants.add(base + e)
        for v in variants:
            cands.add(v)
            cands.add("data/" + v.lstrip("/"))
            cands.add(v.replace("\\", "/"))
            cands.add(v.replace("/", "\\"))
print("candidates:", len(cands))

hit = []
for s in cands:
    h = pvfs_hash(s)
    if h in remaining:
        known[h] = s
        remaining.discard(h)
        hit.append(s)
print("newly named:", len(hit), "remaining:", len(remaining))
for s in sorted(hit):
    print("  ", s)
json.dump({f"{h:08x}": n for h, n in known.items()}, open(NAMES_JSON, "w"), indent=0)
