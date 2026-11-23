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
    print(f"Max metatile in NewBarkTown: {max_id}")
    print(f"Size of johto_metatiles.bin: {size} bytes")

    if size % 4 == 0 and size // 4 >= max_id + 1:
        fmt, n, tiles = "u8", size // 4, raw
    elif size % 8 == 0 and size // 8 >= max_id + 1:
        fmt, n = "u16", size // 8
        tiles = bytearray()
        for i in range(n):
            for j in range(4):
                t = struct.unpack("<H", raw[i*8+j*2:i*8+j*2+2])[0]
                if t > 255:
                    sys.exit(f"tile ID {t} > 255 in metatile {i}, does not fit in u8")
                tiles.append(t)
    else:
        sys.exit(f"Unknown format: size={size}, max_id={max_id}")

    print(f"Detected format: {fmt}, {n} metatiles")

    OUT.parent.mkdir(parents=True, exist_ok=True)
    with open(OUT, "wb") as f:
        f.write(b"JBLK")
        f.write(struct.pack("<H", n))
        f.write(tiles)
    print(f"OK: {OUT} ({n} metatiles, {OUT.stat().st_size} bytes)")

if __name__ == "__main__":
    main()
