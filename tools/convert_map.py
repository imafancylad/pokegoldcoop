#!/usr/bin/env python3
import struct, sys
from pathlib import Path

ROOT = Path.home() / "proyectos/pokegold"

MAPS = {
    "NewBarkTown": {"w": 10, "h": 9,  "out": "newbark_map.bin"},
    "Route29":     {"w": 30, "h": 9,  "out": "route29_map.bin"},
    "CherrygroveCity":{"w": 20, "h": 9, "out": "cherrygrove_map.bin"},
    "Route30":         {"w": 10, "h": 27, "out": "route30_map.bin"},
    "PlayersHouse1F":  {"w": 5,  "h": 4,  "out": "players_house_map.bin"},
    "ElmsLab":         {"w": 6,  "h": 5,  "out": "elms_lab_map.bin"},
}

def main():
    for name, d in MAPS.items():
        blk_path = ROOT / f"maps/{name}.blk"
        out_path = Path("data/generated") / d["out"]
        
        if not blk_path.exists():
            print(f"WARNING: {blk_path} not found")
            continue

        blk = blk_path.read_bytes()
        expected = d["w"] * d["h"]
        if len(blk) != expected:
            print(f"WARNING: {name} expected {expected} blocks, had {len(blk)}. adjusting...")
            d["w"] = len(blk) // d["h"]
            if d["w"] * d["h"] != len(blk):
                print(f"ERROR: cannot calculate dimensions of {name}")
                continue

        out_path.parent.mkdir(parents=True, exist_ok=True)
        with open(out_path, "wb") as f:
            f.write(b"JMAP")
            f.write(struct.pack("<HH", d["w"], d["h"]))
            f.write(blk)
        print(f"OK: {out_path} ({d['w']}x{d['h']}, {len(blk)} blocks)")

if __name__ == "__main__":
    main()