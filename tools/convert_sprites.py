#!/usr/bin/env python3
import struct
from pathlib import Path
from PIL import Image

ROOT = Path.home() / "proyectos/pokegold/gfx/sprites"
OUT = Path("data/generated/sprites.rgba")

SPRITES = {
    "chris": ROOT / "chris.png",
    "rival": ROOT / "rival.png"
}

def main():
    TEX_W = 128
    TEX_H = 128
    out = bytearray(TEX_W * TEX_H * 4)

    for idx, (name, path) in enumerate(SPRITES.items()):
        if not path.exists():
            print(f"WARNING: No se encuentra {path}")
            continue

        img = Image.open(path).convert("RGBA")
        px = img.load()
        
        base_x = idx * 16
        base_y = 0

        for y in range(16):
            for x in range(16):
                r, g, b, a = px[x, y]
                off = ((base_y + y) * TEX_W + (base_x + x)) * 4
                out[off+0] = r
                out[off+1] = g
                out[off+2] = b
                out[off+3] = a

    OUT.parent.mkdir(parents=True, exist_ok=True)
    with open(OUT, "wb") as f:
        f.write(b"SPR1")
        f.write(struct.pack("<HH", TEX_W, TEX_H))
        f.write(out)
    print(f"OK: {OUT} ({len(out)} bytes)")

if __name__ == "__main__":
    main()