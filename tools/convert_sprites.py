#!/usr/bin/env python3
"""Build sprites.rgba with 8 frames per player (down x2, up x2, left x2, right x2)."""
import struct
from pathlib import Path
from PIL import Image, ImageOps

ROOT = Path.home() / "proyectos/pokegold/gfx/sprites"
OUT = Path("data/generated/sprites.rgba")

SPRITES = {
    "chris": ROOT / "chris.png",
    "rival": ROOT / "rival.png",
}

def extract_frames(img):
    """Returns 6 16x16 frames (down stand, down walk, up stand, up walk, left stand, left walk)."""
    frames = []
    for i in range(6):
        top = i * 16
        if top + 16 > img.height:
            frames.append(Image.new("RGBA", (16, 16), (0, 0, 0, 0)))
            continue
        frames.append(img.crop((0, top, 16, top + 16)))
    return frames

def main():
    TEX_W = 128
    TEX_H = 32
    out = bytearray(TEX_W * TEX_H * 4)

    for idx, (name, path) in enumerate(SPRITES.items()):
        if not path.exists():
            print(f"WARNING: {path} no existe, saltando")
            continue

        img = Image.open(path).convert("RGBA")
        print(f"{name}: {img.width}x{img.height}")

        f = extract_frames(img)
        variants = [
            f[0], f[3],
            f[1], f[4],
            f[2], f[5],
            ImageOps.mirror(f[2]), ImageOps.mirror(f[5])
        ]

        base_y = idx * 16
        for i, frame in enumerate(variants):
            base_x = i * 16
            px = frame.load()
            for y in range(16):
                for x in range(16):
                    r, g, b, a = px[x, y]
                    off = ((base_y + y) * TEX_W + (base_x + x)) * 4
                    if a < 32:
                        out[off+0] = 0
                        out[off+1] = 0
                        out[off+2] = 0
                        out[off+3] = 0
                    else:
                        out[off+0] = r
                        out[off+1] = g
                        out[off+2] = b
                        out[off+3] = 255

    img = Image.open(SPRITES["chris"]).convert("RGBA")
    f0 = img.crop((0, 0, 16, 16))
    f1 = img.crop((0, 16, 16, 32))
    diff = sum(1 for x in range(16) for y in range(16) if f0.getpixel((x,y)) != f1.getpixel((x,y)))
    print(f"different pixel frame 0 and 1 from Chris: {diff}/256")

    OUT.parent.mkdir(parents=True, exist_ok=True)
    with open(OUT, "wb") as f:
        f.write(b"SPR1")
        f.write(struct.pack("<HH", TEX_W, TEX_H))
        f.write(out)
    print(f"OK: {OUT} ({len(out)} bytes)")

if __name__ == "__main__":
    main()