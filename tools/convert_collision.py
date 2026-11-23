#!/usr/bin/env python3
"""Convert johto_collision.asm to a simple 1-byte-per-tile binary."""
import struct, sys
from pathlib import Path

ROOT      = Path.home() / "proyectos/pokegold"
COLL_ASM  = ROOT / "data/tilesets/johto_collision.asm"
OUT       = Path("data/generated/johto_collision.bin")

COLLISION_MAP = {
    "FLOOR": 0, "WALL": 1, "WALK_IN_PLACE": 2, "WATER": 3, "TALL_GRASS": 4,
    "WHIRLPOOL": 5, "DOOR": 6, "ICE": 7, "SAND": 8, "LEDGE_DOWN": 9,
    "LEDGE_RIGHT": 10, "LEDGE_LEFT": 11, "LEDGE_UP": 12, "SLIDE_DOWN": 13,
    "SLIDE_RIGHT": 14, "SLIDE_LEFT": 15, "SLIDE_UP": 16, "TURN_LEFT": 17,
    "TURN_RIGHT": 18, "TURN_UP": 19, "TURN_DOWN": 20, "WARP": 21,
    "WARP_CARPET_DOWN": 22, "WARP_CARPET_UP": 23, "WARP_PANEL": 24,
    "WARP_UP": 25, "WARP_DOWN": 26, "WARP_RIGHT": 27, "WARP_LEFT": 28,
    "CURRENT_DOWN": 29, "CURRENT_UP": 30, "CURRENT_LEFT": 31,
    "CURRENT_RIGHT": 32, "HEAL": 33,
}

WALKABLE = {0, 2, 4, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31, 32, 33}

def main():
    if not COLL_ASM.exists():
        sys.exit(f"No se encuentra {COLL_ASM}")

    collisions = []
    with open(COLL_ASM) as f:
        for line in f:
            s = line.strip()
            if s.startswith("tilecoll"):
                s = s.split(';')[0].strip()
                parts = s.split(None, 1)[1].split(",")
                for p in parts:
                    p = p.strip()
                    if not p:
                        continue
                    
                    if p in COLLISION_MAP:
                        val = COLLISION_MAP[p]
                    else:
                        try:
                            val = int(p, 16)
                        except ValueError:
                            val = 0
                    
                    if val in WALKABLE:
                        collisions.append(0)
                    else:
                        collisions.append(1)

    OUT.parent.mkdir(parents=True, exist_ok=True)
    with open(OUT, "wb") as f:
        f.write(b"JCOL")
        f.write(struct.pack("<H", len(collisions)))
        f.write(bytes(collisions))
    print(f"OK: {OUT} ({len(collisions)} tiles, {OUT.stat().st_size} bytes)")

if __name__ == "__main__":
    main()