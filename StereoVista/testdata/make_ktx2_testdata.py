#!/usr/bin/env python3
"""Generates the KTX2/Basis texture test data the M2 harness consumes
(see README.md for the harness itself and for building the basisu encoder).

    python3 make_ktx2_testdata.py <path-to-basisu-encoder> [outdir]

Writes into outdir (default "."):
  check4.png       4x4 black/white checkerboard — every 2x2 box-filter window
                   averages two 0s and two 255s: linear 0.5 -> sRGB 188, so a
                   naive (non-sRGB) mip downsample is detectable (127/128).
  grad5x3.png      5x3 gradient — odd-dimension mip chain (5x3 -> 2x1 -> 1x1).
  src62x37.png     62x37 pattern — non-pow2, odd-dim basisu encode source.
  checker64.png    the 64x64 checker the synthetic packages embed.
  etc1s.ktx2       ETC1S + BasisLZ, mipped         (basisu -ktx2 -mipmap)
  uastc.ktx2       UASTC + zstd, mipped            (… -uastc -ktx2_zstandard_level 6)
  etc1s.basis      legacy .basis container, mipped (basisu -basis -mipmap)
  checker64.ktx2   UASTC + zstd from checker64.png
  synthetic_17_ktx2.slpk
                   synthetic_17_textured.slpk rewritten with BOTH png and ktx2
                   texture entries and textureSetDefinitions listing png BEFORE
                   ktx2 — a loader that prefers compressed formats must land on
                   BC7. STORE zip; hash index rewritten the ArcGIS way.

Requires synthetic_17_textured.slpk next to this script (make_synthetic_slpk.py).
"""
import gzip, hashlib, json, os, struct, subprocess, sys, zipfile, zlib

if len(sys.argv) < 2:
    sys.exit("usage: make_ktx2_testdata.py <path-to-basisu-encoder> [outdir]")
BASISU = os.path.abspath(sys.argv[1])
OUT = os.path.abspath(sys.argv[2] if len(sys.argv) > 2 else ".")
HERE = os.path.dirname(os.path.abspath(__file__))
os.makedirs(OUT, exist_ok=True)


def make_png(width, height, pixel_fn):
    def chunk(tag, payload):
        data = tag + payload
        return struct.pack(">I", len(payload)) + data + struct.pack(
            ">I", zlib.crc32(data) & 0xFFFFFFFF)

    raw = b""
    for y in range(height):
        raw += b"\x00"  # filter: none
        for x in range(width):
            raw += bytes(pixel_fn(x, y))
    ihdr = struct.pack(">IIBBBBB", width, height, 8, 6, 0, 0, 0)
    return (b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", ihdr) +
            chunk(b"IDAT", zlib.compress(raw, 6)) + chunk(b"IEND", b""))


def write(name, data):
    with open(os.path.join(OUT, name), "wb") as f:
        f.write(data)


write("check4.png", make_png(4, 4, lambda x, y:
      (255, 255, 255, 255) if (x + y) % 2 == 0 else (0, 0, 0, 255)))
write("grad5x3.png", make_png(5, 3, lambda x, y: (x * 50, y * 80, 128, 255)))
write("src62x37.png", make_png(62, 37, lambda x, y:
      (x * 4 % 256, y * 7 % 256, (x ^ y) * 5 % 256, 255)))
write("checker64.png", make_png(64, 64, lambda x, y:
      (230, 60, 40, 255) if ((x // 8 + y // 8) % 2) == 0 else
      (240, 235, 220, 255)))


def run(*args):
    subprocess.run(list(args), check=True, cwd=OUT, stdout=subprocess.DEVNULL)


run(BASISU, "-ktx2", "-mipmap", "src62x37.png", "-output_file", "etc1s.ktx2")
run(BASISU, "-ktx2", "-uastc", "-mipmap", "-ktx2_zstandard_level", "6",
    "src62x37.png", "-output_file", "uastc.ktx2")
run(BASISU, "-basis", "-mipmap", "src62x37.png", "-output_file", "etc1s.basis")
run(BASISU, "-ktx2", "-uastc", "-mipmap", "-ktx2_zstandard_level", "6",
    "checker64.png", "-output_file", "checker64.ktx2")

# ---- ktx2-variant synthetic package -------------------------------------------
src = zipfile.ZipFile(os.path.join(HERE, "synthetic_17_textured.slpk"))
entries = []
for zi in src.infolist():
    if zi.filename == "@specialIndexFileHASH128@":
        continue  # rewritten below (offsets change)
    data = src.read(zi.filename)
    if zi.filename == "3dSceneLayer.json.gz":
        layer = json.loads(gzip.decompress(data))
        # png listed FIRST: the loader must still pick ktx2 (compressed
        # formats decode first).
        layer["textureSetDefinitions"] = [{
            "formats": [{"name": "0", "format": "png"},
                        {"name": "0", "format": "ktx2"}],
            "atlas": False,
        }]
        data = gzip.compress(json.dumps(layer).encode(), 6)
    entries.append((zi.filename, data))

with open(os.path.join(OUT, "checker64.ktx2"), "rb") as f:
    ktx2 = f.read()
for res in (0, 1, 2):
    entries.append((f"nodes/{res}/textures/0.ktx2", ktx2))

out_path = os.path.join(OUT, "synthetic_17_ktx2.slpk")
zf = zipfile.ZipFile(out_path, "w", zipfile.ZIP_STORED)
for name, data in entries:
    zf.writestr(zipfile.ZipInfo(name), data)
offsets = {zi.filename: zi.header_offset for zi in zf.infolist()}
recs = sorted((hashlib.md5(n.encode()).digest(), off)
              for n, off in offsets.items())
blob = b"".join(md5 + struct.pack("<Q", off) for md5, off in recs)
zf.writestr(zipfile.ZipInfo("@specialIndexFileHASH128@"), blob)
zf.close()

print("wrote KTX2 test data +", os.path.basename(out_path), "to", OUT)
