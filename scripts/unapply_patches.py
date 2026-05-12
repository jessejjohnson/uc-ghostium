#!/usr/bin/env python3
"""Reverse ``apply_patches.py``: unapply Ghostium's series, then UC's.

Order is reversed: Ghostium's patches are reverted first (they were applied
last), then UC's. Patches whose reverse does not cleanly apply are skipped
with a warning so this script is safe to run on partially-applied trees.
"""

from __future__ import annotations

import argparse
import os
import subprocess
import sys
from pathlib import Path


GHOSTIUM_ROOT = Path(__file__).resolve().parent.parent
UC_ROOT = GHOSTIUM_ROOT / "third_party" / "ungoogled-chromium"
GHOSTIUM_PATCHES_DIR = GHOSTIUM_ROOT / "patches" / "ghostium"


def read_series(series_file: Path) -> list[str]:
    if not series_file.is_file():
        return []
    entries: list[str] = []
    for raw in series_file.read_text().splitlines():
        line = raw.strip()
        if not line or line.startswith("#"):
            continue
        entries.append(line)
    return entries


def unapply_one(patch_path: Path, chromium_src: Path) -> None:
    if not patch_path.is_file():
        print(
            f"[unapply_patches] skipping missing patch: {patch_path}",
            file=sys.stderr,
        )
        return
    check = subprocess.run(
        ["git", "apply", "--check", "-R", str(patch_path)],
        cwd=chromium_src,
    )
    if check.returncode != 0:
        print(
            f"[unapply_patches] skipping (not cleanly reversible): "
            f"{patch_path.relative_to(GHOSTIUM_ROOT)}"
        )
        return
    print(
        f"[unapply_patches] reverting "
        f"{patch_path.relative_to(GHOSTIUM_ROOT)}"
    )
    subprocess.run(
        ["git", "apply", "-R", str(patch_path)],
        cwd=chromium_src,
        check=True,
    )


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--chromium-src",
        default=os.environ.get("CHROMIUM_SRC"),
        help="Path to the Chromium checkout (defaults to $CHROMIUM_SRC).",
    )
    parser.add_argument(
        "--skip-uc",
        action="store_true",
        help="Skip UC's series.",
    )
    args = parser.parse_args()

    if not args.chromium_src:
        print(
            "error: CHROMIUM_SRC is not set and --chromium-src was not "
            "provided.",
            file=sys.stderr,
        )
        return 2

    chromium_src = Path(args.chromium_src).resolve()

    ghostium_series_file = GHOSTIUM_PATCHES_DIR / "series"
    for entry in reversed(read_series(ghostium_series_file)):
        unapply_one(GHOSTIUM_PATCHES_DIR / entry, chromium_src)

    if not args.skip_uc:
        uc_series_file = UC_ROOT / "patches" / "series"
        uc_patches_dir = UC_ROOT / "patches"
        for entry in reversed(read_series(uc_series_file)):
            unapply_one(uc_patches_dir / entry, chromium_src)

    print("[unapply_patches] done.")
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except subprocess.CalledProcessError as exc:
        print(f"[unapply_patches] failed: {exc}", file=sys.stderr)
        sys.exit(exc.returncode or 1)
