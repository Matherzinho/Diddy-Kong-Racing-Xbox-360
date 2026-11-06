import struct, sys, zlib, glob, os

def convert(src, dst, scale=2):
    data = open(src, "rb").read()
    parts = data.split(b"\n", 3)
    w, h = map(int, parts[1].split())
    px = parts[3]
    nw, nh = w // scale, h // scale
    rows = []
    for y in range(nh):
        row = bytearray(b"\x00")
        base = (y * scale) * w * 3
        for x in range(nw):
            o = base + x * scale * 3
            row += px[o:o + 3]
        rows.append(bytes(row))
    raw = b"".join(rows)
    def chunk(t, d):
        c = struct.pack(">I", len(d)) + t + d
        return c + struct.pack(">I", zlib.crc32(t + d) & 0xffffffff)
    png = b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", nw, nh, 8, 2, 0, 0, 0))
    png += chunk(b"IDAT", zlib.compress(raw, 6)) + chunk(b"IEND", b"")
    open(dst, "wb").write(png)

out = sys.argv[2]
os.makedirs(out, exist_ok=True)
for f in sorted(glob.glob(os.path.join(sys.argv[1], "*.ppm"))):
    convert(f, os.path.join(out, os.path.basename(f)[:-4] + ".png"))
    print(f)
