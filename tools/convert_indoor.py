#!/usr/bin/env python3
"""Convert interior tilesets (players_house, lab) to RGBA 128x128."""
import struct
from pathlib import Path
from PIL import Image

ROOT = Path.home() / "proyectos/pokegold"
PAL_BIN = Path("data/generated/bg_tiles_palettes.bin")

COLOR_NAME_TO_IDX = {
    "GRAY": 0, "RED": 1, "GREEN": 2, "WATER": 3,
    "YELLOW": 4, "BROWN": 5, "ROOF": 6, "TEXT": 7,
}

INDOOR_OFFSET = 32

TILESETS = [
    ("players_house", ROOT / "gfx/tilesets/players_house.png",
                      ROOT / "gfx/tilesets/players_house_palette_map.asm"),
    ("lab",           ROOT / "gfx/tilesets/lab.png",
                      ROOT / "gfx/tilesets/lab_palette_map.asm"),
]

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
    return entries

def convert(name, png_path, pal_map_path, palettes):
    if not png_path.exists() or not pal_map_path.exists():
        print(f"WARNING: falta {png_path} o {pal_map_path}, saltando")
        return

    tile_pal_idx = parse_tilepal(pal_map_path)
    img = Image.open(png_path).convert("L")
    W, H = img.size
    TW, TH = W // 8, H // 8
    px = img.load()
    out = bytearray(TEX_W * TEX_H * 4)

    for tile_id in range(TW * TH):
        if tile_id >= len(tile_pal_idx):
            break
        pidx = tile_pal_idx[tile_id]
        if INDOOR_OFFSET + pidx >= len(palettes):
            continue
        palette = palettes[INDOOR_OFFSET + pidx]
        tx = tile_id % TW
        ty = tile_id // TW
        new_tx = tile_id % 16
        new_ty = tile_id // 16
        base = (new_ty * 8 * TEX_W + new_tx * 8) * 4
        for y in range(8):
            for x in range(8):
                v = px[tx*8 + x, ty*8 + y]
                color_idx = min(v // 85, 3)
                r, g, b, a = palette[color_idx]
                off = base + (y * TEX_W + x) * 4
                out[off+0] = r
                out[off+1] = g
                out[off+2] = b
                out[off+3] = 255

    out_path = Path(f"data/generated/{name}_tiles.rgba")
    with open(out_path, "wb") as f:
        f.write(b"JTL1")
        f.write(struct.pack("<HH", 16, 16))
        f.write(b"\x00" * 8)
        f.write(out)
    print(f"OK: {out_path} ({len(out)} bytes, {TW}x{TH} tiles)")

def main():
    palettes = load_palettes(PAL_BIN)
    for name, png, pal in TILESETS:
        convert(name, png, pal, palettes)

if __name__ == "__main__":
    main()