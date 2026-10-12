import struct

b = open(r"C:\Users\Kim Malale\Desktop\workdesk\raging thunder\extracted_clean\data\car\00\car.pxc", "rb").read()

def show(off, n, label):
    chunk = b[off:off + n]
    print(f"--- {label} @{off}")
    print("   ", chunk.hex(" "))
    print("   ", "".join(chr(c) if 32 <= c < 127 else "." for c in chunk))

# body MESH frame: tag @1664, size @1668, payload @1672
show(1664, 40, "MESH frame header + geometry start")
# expected ib header at 19664 (if geometry = 28 + 12 + 17952)
show(19656, 40, "expected ib header")
# expected matCount at 22424
show(22416, 40, "expected matCount")
print()
print("u32 at 1672:", struct.unpack_from("<I", b, 1672)[0])
print("fixed at 1676:", struct.unpack_from("<i", b, 1676)[0] / 65536.0)
print("u32 at 19664:", struct.unpack_from("<I", b, 19664)[0])
print("u32 at 19668:", struct.unpack_from("<I", b, 19668)[0])
print("u32 at 22420:", struct.unpack_from("<I", b, 22420)[0])
print("u32 at 22424:", struct.unpack_from("<I", b, 22424)[0])
print("u32 at 22428:", struct.unpack_from("<I", b, 22428)[0])
