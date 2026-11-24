#!/usr/bin/env python3
"""Convert johto.png + palettes + tilepal into an RGBA tileset (128x128)."""
import re, struct, sys
from pathlib import Path
from PIL import Image

ROOT      = Path.home() / "proyectos/pokegold"
TILES_PNG = ROOT / "gfx/tilesets/johto.png"
MAP_ASM   = ROOT / "maps/NewBarkTown.asm"
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

# PS2 GS standard dimensions
TEX_W = 128
TEX_H = 128
TILES_W = TEX_W // 8
TILES_H = TEX_H // 8

def load_palettes(path):
    with open(path, "rb") as f:
        assert f.read(4) == b"BPAL", "wrong magic in bg_tiles_palettes.bin"
        count = struct.unpack("<H", f.read(2))[0]
        palettes = []
        for _ in range(count):
            colors = []
            for _ in range(4):
                r, g, b, a = f.read(4)
                colors.append((r, g, b, a))
            palettes.append(colors)
    return palettes

def parse_tilepal_from_file(path):
    entries = []
    if not path.exists():
        print(f"DEBUG: {path} not found.")
        return entries
    with open(path, encoding="utf-8") as f:
        for line in f:
            s = line.strip()
            if "tilepal" in s:
                parts = s.split(",")
                for name in parts[1:]:
                    name = name.strip()
                    if name == "GRAY":
                        name = "GRAY"
                    if name in COLOR_NAME_TO_IDX:
                        entries.append(COLOR_NAME_TO_IDX[name])
    return entries

def main():
    palettes = load_palettes(PAL_BIN)
    tile_pal_idx = parse_tilepal_from_file(MAP_ASM)
    if len(tile_pal_idx) < 96:
        print("Using generic Johto palette map...")
        tile_pal_idx = parse_tilepal_from_file(PAL_MAP)
        
    if len(tile_pal_idx) < 96:
        raise ValueError(f"Could not find 96 palettes in any file.")
    
    tile_pal_idx = tile_pal_idx[:96]

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
            
            new_tx = tile_id % TILES_W
            new_ty = tile_id // TILES_W
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
                    out[off+3] = a
            tile_id += 1

    OUT.parent.mkdir(parents=True, exist_ok=True)
    with open(OUT, "wb") as f:
        f.write(b"JTL1")
        f.write(struct.pack("<HH", TILES_W, TILES_H))
        f.write(b"\x00" * 8) # Padding 16-byte alignment
        f.write(out)
    print(f"OK: {OUT}  ({TILES_W}x{TILES_H} tiles, {OUT.stat().st_size} bytes)")

if __name__ == "__main__":
    main()