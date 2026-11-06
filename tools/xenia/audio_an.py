import struct, sys, wave
raw = open(sys.argv[1], "rb").read()
n = len(raw) // 2
be = struct.unpack(">%dh" % n, raw[: n * 2])
le = struct.unpack("<%dh" % n, raw[: n * 2])

def stats(s, name):
    # roughness: mean abs difference between consecutive samples of the same channel
    d = sum(abs(s[i] - s[i - 2]) for i in range(2, len(s), 7)) / (len(s) // 7)
    a = sum(abs(x) for x in s[::7]) / (len(s) // 7)
    clip = sum(1 for x in s[::7] if abs(x) >= 32760) / (len(s) // 7)
    nz = sum(1 for x in s[::7] if x != 0) / (len(s) // 7)
    print(f"{name}: mean|x| {a:.0f}, mean|dx| {d:.0f}, clipped {clip*100:.2f}%, nonzero {nz*100:.1f}%")

stats(be, "as big-endian")
stats(le, "as little-endian")
# per-second profile (22050 Hz stereo)
sec = 22050 * 2
for k in range(0, min(n // sec, 90), 5):
    seg = be[k * sec:(k + 1) * sec]
    print(k, "s: peak", max(abs(x) for x in seg), "mean", sum(abs(x) for x in seg[::5]) // (len(seg) // 5),
          "clip%", round(100 * sum(1 for x in seg if abs(x) >= 32760) / len(seg), 2))
w = wave.open(sys.argv[2], "wb")
w.setnchannels(2)
w.setsampwidth(2)
w.setframerate(22050)
w.writeframes(struct.pack("<%dh" % n, *be))
w.close()
