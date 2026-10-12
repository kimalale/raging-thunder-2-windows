import struct

b = open(r"C:\Users\Kim Malale\Desktop\workdesk\raging thunder\extracted_clean\data\car\00\car.pxc", "rb").read()
offs = []
i = 0
while True:
    i = b.find(b"TEXT", i)
    if i < 0:
        break
    offs.append(i)
    i += 4
offs = offs[:6]
with open(r"C:\Users\Kim Malale\Desktop\workdesk\raging thunder\work\matdump.txt", "w") as f:
    for off in offs:
        f.write(f"=== TEXT at {off}\n")
        chunk = b[off - 24:off + 96]
        f.write(chunk.hex(" ") + "\n")
        f.write("".join(chr(c) if 32 <= c < 127 else "." for c in chunk) + "\n\n")
print("written", offs)
