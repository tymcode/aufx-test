#!/usr/bin/env python3
"""Import DEEP/Z .aupreset files into compares/deepz/presets/au as {NN}-{effectName}.aupreset.

Reads effect / effectName from the embedded DEEPZ_STATE XML. Use after saving
AU presets from Logic or AU Effects Explorer for factory effects that are not
yet in the compares tree.

Example:
  # Save factory effects from the plugin UI into a folder, then:
  python3 scripts/import_deepz_aupresets.py ~/Desktop/deepz-exports
  python3 scripts/import_deepz_aupresets.py --from-library
"""

from __future__ import annotations

import argparse
import plistlib
import re
import shutil
import sys
from pathlib import Path


def extract_effect(path: Path) -> tuple[int, str] | None:
    data = path.read_bytes()
    match = re.search(rb'<DEEPZ_STATE\b[^>]*?/?>', data)
    if match is None:
        try:
            outer = plistlib.loads(data)
        except Exception:
            return None
        stack: list[object] = [outer]
        while stack:
            obj = stack.pop()
            if isinstance(obj, dict):
                for value in obj.values():
                    if isinstance(value, (bytes, bytearray)) and b"DEEPZ_STATE" in value:
                        match = re.search(rb'<DEEPZ_STATE\b[^>]*?/?>', value)
                        if match:
                            break
                    elif isinstance(value, (bytes, bytearray)):
                        try:
                            stack.append(plistlib.loads(value))
                        except Exception:
                            pass
                    elif isinstance(value, dict):
                        stack.append(value)
            if match is not None:
                break
    if match is None:
        return None
    tag = match.group(0).decode("ascii", errors="replace")
    effect = re.search(r'\beffect="(\d+)"', tag)
    name = re.search(r'\beffectName="([^"]*)"', tag)
    if not effect or not name:
        return None
    return int(effect.group(1)), name.group(1)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "sources",
        nargs="*",
        type=Path,
        help="Files or directories of .aupreset to import",
    )
    parser.add_argument(
        "--from-library",
        action="store_true",
        help="Also scan ~/Library/Audio/Presets/Temecula DSP/DEEP*",
    )
    parser.add_argument(
        "--out",
        type=Path,
        default=Path("compares/deepz/presets/au"),
        help="Destination presets/au folder",
    )
    parser.add_argument(
        "--force",
        action="store_true",
        help="Overwrite existing destination files",
    )
    parser.add_argument(
        "--dry-run",
        action="store_true",
        help="Print actions without copying",
    )
    args = parser.parse_args()

    files: list[Path] = []
    for src in args.sources:
        src = src.expanduser()
        if src.is_file() and src.suffix.lower() == ".aupreset":
            files.append(src)
        elif src.is_dir():
            files.extend(sorted(src.rglob("*.aupreset")))
        else:
            print(f"warning: skip missing path {src}", file=sys.stderr)

    if args.from_library:
        lib = Path.home() / "Library/Audio/Presets/Temecula DSP"
        for pattern in ("DEEP:Z", "DEEP/Z", "DEEP:Z/**", "DEEP/Z/**"):
            files.extend(sorted(lib.glob(f"{pattern}/*.aupreset")))

    # Deduplicate by resolve
    unique: list[Path] = []
    seen: set[Path] = set()
    for path in files:
        key = path.resolve()
        if key in seen:
            continue
        seen.add(key)
        unique.append(path)

    if not unique:
        print("error: no .aupreset files found", file=sys.stderr)
        return 2

    args.out.mkdir(parents=True, exist_ok=True)
    imported = 0
    skipped = 0
    for path in unique:
        info = extract_effect(path)
        if info is None:
            print(f"skip (no DEEPZ_STATE): {path}")
            skipped += 1
            continue
        number, name = info
        dest_name = f"{number:02d}-{name}.aupreset"
        dest = args.out / dest_name
        if dest.exists() and not args.force:
            print(f"skip exists: {dest_name}  (from {path.name})")
            skipped += 1
            continue
        print(f"{'would copy' if args.dry_run else 'copy'} {path.name} -> {dest_name}")
        if not args.dry_run:
            shutil.copy2(path, dest)
        imported += 1

    print(f"done: {imported} imported, {skipped} skipped -> {args.out}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
