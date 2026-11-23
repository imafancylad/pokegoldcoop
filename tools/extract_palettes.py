#!/usr/bin/env python3
"""Extracts the 5×8 palettes from bg_tiles.pal into a simple binary."""
import re, struct, sys
from pathlib import Path

PAL_REF = Path.home() / "proyectos/pokegold/gfx/tilesets/bg_tiles.pal"
OUT     = Path("data/generated/bg_tiles_palettes.bin")

SECTIONS = ("morn", "day", "nite", "dark", "indoor")

def rgb5_to_rgb8(v):
    return (v << 3) | (v >> 2)

def parse_pal(path):
    sets = {name: [] for name in SECTIONS}
    cur = None
    with open(path) as f:
        for line in f:
            s = line.strip()
            if not s:
                continue
            if s.startswith(";"):
                name = s[1:].strip().lower()
                if name in SECTIONS:
                    cur = name
                continue

            if cur is not None and s.upper().startswith("RGB"):
                data = s.split(";", 1)[0].strip()
                data = data[3:].strip()
                nums = [int(x.strip()) for x in data.split(",")]
                if len(nums) != 12:
                    print(f"WARNING: {cur}: expected 12 values, got {len(nums)}",
                          file=sys.stderr)
                    continue
                rgb = []
                for i in range(4):
                    r5, g5, b5 = nums[i*3:i*3+3]
                    rgb.append((rgb5_to_rgb8(r5), rgb5_to_rgb8(g5), rgb5_to_rgb8(b5)))
                sets[cur].append(rgb)
    return sets

def main():
    sets = parse_pal(PAL_REF)
    flat = []
    for name in SECTIONS:
        p = sets[name]
        if len(p) != 8:
            print(f"WARNING: '{name}' has {len(p)} palettes (expected 8)",
                  file=sys.stderr)
        flat.extend(p)

    OUT.parent.mkdir(parents=True, exist_ok=True)
    with open(OUT, "wb") as f:
        f.write(b"BPAL")
        f.write(struct.pack("<H", len(flat)))
        for pal in flat:
            for (r, g, b) in pal:
                f.write(bytes((r, g, b, 0xFF)))
    print(f"OK: {OUT}  ({len(flat)} palettes, {OUT.stat().st_size} bytes)")
    for name in SECTIONS:
        print(f"   {name}: {len(sets[name])} palettes")

if __name__ == "__main__":
    main()
