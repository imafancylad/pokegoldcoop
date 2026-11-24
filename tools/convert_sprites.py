#!/usr/bin/env python3
"""Build sprites.rgba with 8 frames per player and transparent background."""
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
            print(f"WARNING: {path} no existe")
            continue

        img = Image.open(path).convert("RGBA")
        
        bg_color = img.getpixel((0, 0))[:3]
        
        datas = img.getdata()
        new_data = []
        for item in datas:
            if item[:3] == bg_color:
                new_data.append((0, 0, 0, 0))
            else:
                new_data.append(item)
        img.putdata(new_data)

        f = extract_frames(img)
        # f[0], f[1] = down (stand, walk)
        # f[2], f[3] = left (stand, walk)
        # f[4], f[5] = up (stand, walk)
        
        variants = [
            f[0], f[1],
            f[4], f[5],
            f[2], f[3],
            ImageOps.mirror(f[2]), ImageOps.mirror(f[3])
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

    OUT.parent.mkdir(parents=True, exist_ok=True)
    with open(OUT, "wb") as f:
        f.write(b"SPR1")
        f.write(struct.pack("<HH", TEX_W, TEX_H))
        f.write(out)
    print(f"OK: {OUT} ({len(out)} bytes)")

if __name__ == "__main__":
    main()