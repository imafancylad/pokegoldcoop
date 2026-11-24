#!/usr/bin/env python3
"""Convert johto.png + real palettes into an RGBA tileset (128x128)."""
import re, struct
from pathlib import Path
from PIL import Image

ROOT      = Path.home() / "proyectos/pokegold"
TILES_PNG = ROOT / "gfx/tilesets/johto.png"
PAL_MAP   = ROOT / "gfx/tilesets/johto_palette_map.asm"
PAL_BIN   = Path("data/generated/bg_tiles_palettes.bin")
OUT       = Path("data/generated/johto_tiles.rgba")

COLOR_NAME_TO_IDX = {
    "GRAY": 0, "RED": 1, "GREEN": 2, "WATER": 3,
    "YELLOW": 4, "BROWN": 5, "ROOF": 6, "TEXT": 7,
}

PALETTE_SET = "day"
SECTIONS = ("morn", "day", "nite", "dark", "indoor")
SET_OFFSET = SECTIONS.index(PALETTE_SET) * 8

TEX_W = 128
TEX_H = 128

def load_palettes(path):
    with open(path, "rb") as f:
        assert f.read(4) == b"BPAL"
        count = struct.unpack("<H", f.read(2))[0]
        palettes = []
        for _ in range(count):
            colors = []
            for _ in range(4):
                r, g, b, a = f.read(4)
                colors.append((r, g, b, a))
            palettes.append(colors)
    return palettes

def parse_tilepal(path):
    entries = []
    with open(path) as f:
        for line in f:
            s = line.strip()
            if s.startswith("tilepal"):
                parts = s.split(",")
                for name in parts[1:]:
                    name = name.strip()
                    if name in COLOR_NAME_TO_IDX:
                        entries.append(COLOR_NAME_TO_IDX[name])
    return entries[:96] + entries[128:]

def main():
    palettes = load_palettes(PAL_BIN)
    tile_pal_idx = parse_tilepal(PAL_MAP)

    img = Image.open(TILES_PNG).convert("L")
    W, H = img.size
    TW, TH = W // 8, H // 8
    px = img.load()

    out = bytearray(TEX_W * TEX_H * 4)

    tile_id = 0
    for ty in range(TH):
        for tx in range(TW):
            pidx = tile_pal_idx[tile_id]
            palette = palettes[SET_OFFSET + pidx]

            vram_id = tile_id if tile_id < 96 else tile_id + 32
            new_tx = vram_id % 16
            new_ty = vram_id // 16
            base = (new_ty * 8 * TEX_W + new_tx * 8) * 4
            
            new_tx = tile_id % 16
            new_ty = tile_id // 16
            base = (new_ty * 8 * TEX_W + new_tx * 8) * 4

            for y in range(8):
                for x in range(8):
                    v = px[tx*8 + x, ty*8 + y]
                    color_idx = 3 - min(v // 85, 3)
                    
                    r, g, b, a = palette[color_idx]
                    off = base + (y * TEX_W + x) * 4
                    out[off+0] = r
                    out[off+1] = g
                    out[off+2] = b
                    out[off+3] = 255
            tile_id += 1

    OUT.parent.mkdir(parents=True, exist_ok=True)
    with open(OUT, "wb") as f:
        f.write(b"JTL1")
        f.write(struct.pack("<HH", 16, 16))
        f.write(b"\x00" * 8)
        f.write(out)
    print(f"OK: {OUT} ({len(out)} bytes)")

if __name__ == "__main__":
    main()