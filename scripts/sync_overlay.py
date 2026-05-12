#!/usr/bin/env python3
"""Symlink Ghostium's overlay tree into the Chromium source tree.

GN cannot reference paths outside ``//src/...``. Ghostium keeps its overlay
under ``ghostium_src/overlay`` in this repository and exposes it to GN at
``$CHROMIUM_SRC/ghostium_src/overlay`` via a symlink. Editing stays in one
place (this repo); GN sees it as if it were native.

Idempotent: re-running replaces the symlink if the target path changes.
"""

from __future__ import annotations

import argparse
import os
import shutil
import sys
from pathlib import Path


GHOSTIUM_ROOT = Path(__file__).resolve().parent.parent
OVERLAY_ROOT = GHOSTIUM_ROOT / "ghostium_src" / "overlay"


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--chromium-src",
        default=os.environ.get("CHROMIUM_SRC"),
        help="Path to the Chromium checkout (defaults to $CHROMIUM_SRC).",
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
    if not chromium_src.is_dir():
        print(
            f"error: Chromium src directory does not exist: {chromium_src}",
            file=sys.stderr,
        )
        return 2

    if not OVERLAY_ROOT.is_dir():
        print(
            f"error: Ghostium overlay directory does not exist: "
            f"{OVERLAY_ROOT}",
            file=sys.stderr,
        )
        return 2

    target = chromium_src / "ghostium_src" / "overlay"
    target.parent.mkdir(parents=True, exist_ok=True)

    if target.is_symlink():
        if target.readlink() == OVERLAY_ROOT:
            print(f"[sync_overlay] symlink already in place: {target}")
            return 0
        target.unlink()
    elif target.exists():
        shutil.rmtree(target)

    target.symlink_to(OVERLAY_ROOT)
    print(f"[sync_overlay] {target} -> {OVERLAY_ROOT}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
