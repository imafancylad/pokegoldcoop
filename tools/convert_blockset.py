#!/usr/bin/env python3
"""Converts johto_metatiles.bin to a u8×4 blockset (TL, TR, BL, BR per metatile)."""
import struct, sys
from pathlib import Path

ROOT      = Path.home() / "proyectos/pokegold"
METATILES = ROOT / "data/tilesets/johto_metatiles.bin"
BLK       = ROOT / "maps/NewBarkTown.blk"
OUT       = Path("data/generated/johto_blocks.bin")

def main():
    blocksets = [
        ("johto",         ROOT / "data/tilesets/johto_metatiles.bin",         Path("data/generated/johto_blocks.bin")),
        ("players_house", ROOT / "data/tilesets/players_house_metatiles.bin", Path("data/generated/players_house_blocks.bin")),
        ("lab",           ROOT / "data/tilesets/lab_metatiles.bin",           Path("data/generated/lab_blocks.bin")),
    ]

    for name, src, dst in blocksets:
        if not src.exists():
            print(f"WARNING: does not exist {src}")
            continue
        raw = src.read_bytes()
        size = len(raw)
        if size % 16 != 0:
            print(f"WARNING: {name} unexpected size: {size}")
            continue
        n = size // 16
        print(f"{name}: {n} metatiles ({size} bytes)")
        dst.parent.mkdir(parents=True, exist_ok=True)
        with open(dst, "wb") as f:
            f.write(b"JBLK")
            f.write(struct.pack("<H", n))
            f.write(raw)
        print(f"OK: {dst}")

if __name__ == "__main__":
    main()
