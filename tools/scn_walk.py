import struct, sys

path = sys.argv[1] if len(sys.argv) > 1 else r"C:\Users\Kim Malale\Desktop\workdesk\raging thunder\extracted_clean\data\car\00\car.pxc"
b = open(path, "rb").read()
LIMIT = [60]


def walk(data, pos, end, depth=0):
    while pos + 8 <= end and LIMIT[0] > 0:
        tag = data[pos:pos + 4]
        size = struct.unpack_from("<I", data, pos + 4)[0]
        p = pos + 8
        pend = p + size
        if pend > end:
            print("  " * depth + f"@{pos} {tag!r} size={size} OVERRUN")
            return
        info = ""
        if tag in (b"SGGR", b"SGNO"):
            nlen = struct.unpack_from("<H", data, p)[0]
            name = data[p + 2:p + 2 + nlen].decode("latin1", "ignore").rstrip("\x00")
            q = p + 2 + nlen + 4
            n = struct.unpack_from("<I", data, q)[0]
            info = f"name={name!r} children={n}"
        elif tag in (b"SGSP", b"SGPS"):
            nlen = struct.unpack_from("<H", data, p)[0]
            name = data[p + 2:p + 2 + nlen].decode("latin1", "ignore").rstrip("\x00")
            info = f"name={name!r}"
        elif tag == b"TEXT":
            nlen = struct.unpack_from("<H", data, p)[0]
            q = p + 2 + nlen + 4
            plen = struct.unpack_from("<H", data, q)[0]
            pth = data[q + 2:q + 2 + plen].decode("latin1", "ignore").rstrip("\x00")
            info = f"tex={pth!r}"
        elif tag == b"#RES":
            info = "resource-ref-wrapper"
        print("  " * depth + f"@{pos} {tag.decode('latin1')} size={size} {info}")
        LIMIT[0] -= 1
        if tag in (b"SGGR", b"SGNO"):
            walk(data, q + 4, pend, depth + 1)
        pos = pend


print("file:", path, "version:", hex(struct.unpack_from("<I", b, 4)[0]))
walk(b, 8, len(b))
