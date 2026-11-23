#!/usr/bin/env python3
"""Converts NewBarkTown.blk to the internal format {JMAP, w, h, blocks[]}."""
import struct, sys
from pathlib import Path

ROOT = Path.home() / "proyectos/pokegold"
OUT  = Path("data/generated/newbark_map.bin")

# 10 * 9 = 90 blocks matches the .blk size
# if the render looks streaked switch to another combination and try again
MAPS = {
    "NewBarkTown": {"w": 10, "h": 9},
}

def main():
    name = "NewBarkTown"
    d = MAPS[name]
    blk_path = ROOT / f"maps/{name}.blk"
    blk = blk_path.read_bytes()

    expected = d["w"] * d["h"]
    if len(blk) != expected:
        sys.exit(f"{name}: expected {expected} blocks, got {len(blk)}")

    OUT.parent.mkdir(parents=True, exist_ok=True)
    with open(OUT, "wb") as f:
        f.write(b"JMAP")
        f.write(struct.pack("<HH", d["w"], d["h"]))
        f.write(blk)
    print(f"OK: {OUT} ({d['w']}x{d['h']}, {len(blk)} blocks, {OUT.stat().st_size} bytes)")

if __name__ == "__main__":
    main()
