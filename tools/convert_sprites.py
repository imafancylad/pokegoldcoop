#!/usr/bin/env python3
import struct
from pathlib import Path
from PIL import Image, ImageOps

ROOT = Path.home() / "proyectos/pokegold/gfx/sprites"
OUT = Path("data/generated/sprites.rgba")

PALETTE_J1 = [(255, 200, 180), (220, 80, 80), (120, 40, 40), (30, 20, 20)]
PALETTE_J2 = [(200, 220, 255), (80, 120, 220), (40, 60, 120), (20, 20, 30)]
PALETTE_MOM = [(240, 220, 200), (200, 120, 80), (120, 60, 40), (40, 20, 10)]

SPRITES = [
    ("chris", ROOT / "chris.png", PALETTE_J1),
    ("rival", ROOT / "rival.png", PALETTE_J2),
    ("mom",   ROOT / "mom.png",   PALETTE_MOM),
]

def remove_bg(img):
    img = img.convert("RGBA")
    w, h = img.size
    px = img.load()
    bg = px[0, 0][:3]
    q = [(0, 0)]
    seen = set()
    while q:
        x, y = q.pop(0)
        if (x, y) in seen: continue
        seen.add((x, y))
        if px[x, y][:3] == bg:
            px[x, y] = (0, 0, 0, 0)
            if x > 0: q.append((x-1, y))
            if x < w-1: q.append((x+1, y))
            if y > 0: q.append((x, y-1))
            if y < h-1: q.append((x, y+1))
    return img

def main():
    TEX_W, TEX_H = 128, 48
    out = bytearray(TEX_W * TEX_H * 4)

    for idx, (name, path, palette) in enumerate(SPRITES):
        if not path.exists():
            print(f"WARNING: {path} no existe, saltando")
            continue
        img = remove_bg(Image.open(path))
        frames = [img.crop((0, i*16, 16, i*16+16)) for i in range(6)]
        variants = [
            frames[0], frames[3],                                   # down
            frames[1], frames[4],                                   # up
            frames[2], frames[5],                                   # left
            ImageOps.mirror(frames[2]), ImageOps.mirror(frames[5])  # right
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
                        out[off:off+4] = b'\x00\x00\x00\x00'
                    else:
                        lum = (r + g + b) // 3
                        lvl = min(3, lum // 64)
                        cr, cg, cb = palette[lvl]
                        out[off:off+4] = bytes((cr, cg, cb, 255))

    OUT.parent.mkdir(parents=True, exist_ok=True)
    with open(OUT, "wb") as f:
        f.write(b"SPR1")
        f.write(struct.pack("<HH", TEX_W, TEX_H))
        f.write(out)
    print(f"OK: {OUT} ({len(out)} bytes)")

if __name__ == "__main__":
    main()