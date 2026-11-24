#!/usr/bin/env python3
"""Converts johto_metatiles.bin to a u8×4 blockset (TL, TR, BL, BR per metatile)."""
import struct, sys
from pathlib import Path

ROOT      = Path.home() / "proyectos/pokegold"
METATILES = ROOT / "data/tilesets/johto_metatiles.bin"
BLK       = ROOT / "maps/NewBarkTown.blk"
OUT       = Path("data/generated/johto_blocks.bin")

def main():
    blk = BLK.read_bytes()
    max_id = max(blk)
    raw = METATILES.read_bytes()
    size = len(raw)

    if size % 16 != 0:
        sys.exit(f"unexpected size in johto_metatiles.bin: {size} bytes")

    n = size // 16
    if n <= max_id:
        sys.exit(f"metatiles missing: n={n}, max_id={max_id}")

    print(f"Detected format: u8x16 (4x4 tiles), {n} metatiles")

    OUT.parent.mkdir(parents=True, exist_ok=True)
    with open(OUT, "wb") as f:
        f.write(b"JBLK")
        f.write(struct.pack("<H", n))
        f.write(raw)
    print(f"OK: {OUT} ({n} metatiles, {OUT.stat().st_size} bytes)")

if __name__ == "__main__":
    main()
