#!/usr/bin/env python3
"""Convierte johto.png + paletas + tilepal a un tileset RGBA (96 tiles 8x8)."""
import re, struct, sys
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

def load_palettes(path):
    with open(path, "rb") as f:
        assert f.read(4) == b"BPAL", "magic incorrecto en bg_tiles_palettes.bin"
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
            if not s or s.startswith(";"):
                continue
            m = re.search(r"tilepal\s+\d+\s*,\s*(.+)$", s)
            if not m:
                continue
            names = [n.strip() for n in m.group(1).split(",")]
            for name in names:
                if name not in COLOR_NAME_TO_IDX:
                    raise ValueError(f"Paleta desconocida: {name}")
                entries.append(COLOR_NAME_TO_IDX[name])
    if len(entries) < 96:
        raise ValueError(f"tilepal tiene {len(entries)} entradas, se esperan ≥96")
    return entries[:96]

def main():
    palettes = load_palettes(PAL_BIN)
    tile_pal_idx = parse_tilepal(PAL_MAP)

    img = Image.open(TILES_PNG).convert("L")
    W, H = img.size
    TW, TH = W // 8, H // 8
    if TW * TH < 96:
        raise ValueError(f"PNG {W}x{H} solo tiene {TW*TH} tiles")
    px = img.load()

    out = bytearray(TW * TH * 8 * 8 * 4)

    tile_id = 0
    for ty in range(TH):
        for tx in range(TW):
            pidx = tile_pal_idx[tile_id]
            palette = palettes[SET_OFFSET + pidx]
            base = tile_id * 8 * 8 * 4
            for y in range(8):
                for x in range(8):
                    v = px[tx*8 + x, ty*8 + y]
                    color_idx = min(v // 85, 3)
                    r, g, b, a = palette[color_idx]
                    off = base + (y * 8 + x) * 4
                    out[off+0] = r
                    out[off+1] = g
                    out[off+2] = b
                    out[off+3] = a
            tile_id += 1

    OUT.parent.mkdir(parents=True, exist_ok=True)
    with open(OUT, "wb") as f:
        f.write(b"JTL1")
        f.write(struct.pack("<HH", TW, TH))
        f.write(out)
    print(f"OK: {OUT}  ({TW}x{TH} tiles, {OUT.stat().st_size} bytes)")
    print(f"     Paleta usada: '{PALETTE_SET}'")

if __name__ == "__main__":
    main()