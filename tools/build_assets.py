#!/usr/bin/env python3
"""Orchestrates the entire asset conversion pipeline."""
import subprocess, sys
from pathlib import Path

SCRIPTS = [
    "extract_palettes.py",
    "convert_tileset.py",
    "convert_blockset.py",
    "convert_map.py",
]

def main():
    for s in SCRIPTS:
        print(f"\n=== {s} ===")
        r = subprocess.run([sys.executable, str(Path("tools") / s)])
        if r.returncode != 0:
            sys.exit(f"FAILED at {s}")
    print("\nAll assets generated in data/generated/")

if __name__ == "__main__":
    main()
