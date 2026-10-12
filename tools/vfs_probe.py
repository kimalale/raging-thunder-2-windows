import struct, os

VFS = r"C:\Users\Kim Malale\Desktop\workdesk\raging thunder\apk\assets\Data.vfs"
b = open(VFS, "rb").read()
assert b[:4] == b"FUFS"
seed, count, data_off = struct.unpack_from("<III", b, 4)
print("count", count, "data_off", data_off)
toc = []
for i in range(count):
    h, sz, endo = struct.unpack_from("<III", b, 16 + i * 12)
    toc.append((h, sz, endo))

def H(s):
    h = 0
    s2 = []
    prev = None
    for c in s.replace("\\", "/"):
        if c == "/" and prev == "/":
            continue
        prev = c
        s2.append(c.upper() if "a" <= c <= "z" else c)
    for c in s2:
        h = (h * 0x43 + ord(c) - 0x71) & 0xFFFFFFFF
    return h

for name in ["data/track/Track_Tropical_1.scn", "data/track/track_tropical_1.scn",
             "DATA/TRACK/TRACK_TROPICAL_1.SCN", "data\\track\\Track_Tropical_1.scn"]:
    h = H(name)
    hit = [t for t in toc if t[0] == h]
    print(name, hex(h), "HIT" if hit else "miss", hit[:1])

# find the biggest entry and sniff its decompressed header
big = sorted(toc, key=lambda t: -t[1])[:5]
for h, sz, endo in big:
    print("big", hex(h), sz)
