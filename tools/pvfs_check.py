import struct, sys, os, json

VFS = r"C:\Users\Kim Malale\Desktop\workdesk\raging thunder\apk\assets\Data.vfs"
STRINGS = r"C:\Users\Kim Malale\Desktop\workdesk\raging thunder\work\so_x86_strings.txt"

def pvfs_hash(s, normalize=True):
    h = 0
    i = 0
    n = len(s)
    if i < n and s[i] in "/\\":
        i += 1
    while i < n:
        c = s[i]
        i += 1
        if normalize:
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
        else:
            ch = 0x2f if c == '\\' else ord(c)
        h = (h * 0x43 + ch - 0x71) & 0xFFFFFFFF
    return h

data = open(VFS, "rb").read()
magic, seed, count, dataoff = struct.unpack_from("<4sIII", data, 0)
print("magic", magic, "seed", hex(seed), "count", count, "dataoff", dataoff)
toc = []
for i in range(count):
    h, size, endoff = struct.unpack_from("<III", data, 16 + i * 12)
    toc.append((h, size, endoff))
hashes = {t[0] for t in toc}
print("toc entries:", len(toc), "unique:", len(hashes))
print("offsets ok:", all(toc[i][2] - toc[i][1] >= dataoff for i in range(len(toc))))
print("sequential ok:", all(toc[i][2] == toc[i+1][2] - toc[i+1][1] for i in range(len(toc) - 1)))

names = set()
for line in open(STRINGS, "r", encoding="utf-8", errors="ignore"):
    s = line.rstrip("\r\n")
    if len(s) >= 4 and ("/" in s or "\\" in s) and not s.startswith("("):
        names.add(s)

matched = []
for s in names:
    for norm in (True, False):
        h = pvfs_hash(s, norm)
        if h in hashes:
            matched.append((s, hex(h), norm))
print("matched known names:", len(matched))
for m in matched[:40]:
    print("  ", m)

# also test lowercase-only variants of matched names
extra = set()
for s in names:
    extra.add(s.lower())
    extra.add(s.replace("\\", "/"))
matched2 = []
for s in extra:
    for norm in (True, False):
        h = pvfs_hash(s, norm)
        if h in hashes:
            matched2.append((s, hex(h), norm))
uniq = {}
for s, h, norm in matched + matched2:
    uniq.setdefault(h, (s, norm))
print("total distinct file names matched:", len(uniq))
json.dump({h: s for h, (s, n) in uniq.items()}, open(r"C:\Users\Kim Malale\Desktop\workdesk\raging thunder\work\known_names.json", "w"), indent=0)
