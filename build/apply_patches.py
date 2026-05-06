#!/usr/bin/env python3
"""Apply UC's patch series followed by Ghostium's patch series.

UC's own patch series is applied first (inherited from the UC submodule).
Ghostium's series is concatenated after. Any failure aborts the process.

Series files use quilt-style format: one patch filename per line, blank lines
and ``#``-prefixed comments ignored.
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


def apply_one(patch_path: Path, chromium_src: Path) -> None:
    if not patch_path.is_file():
        raise FileNotFoundError(f"Missing patch file: {patch_path}")
    print(f"[apply_patches] applying {patch_path.relative_to(GHOSTIUM_ROOT)}")
    subprocess.run(
        ["git", "apply", "--3way", str(patch_path)],
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
        help="Skip UC's series (useful when testing Ghostium patches in "
             "isolation).",
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
    if not (chromium_src / ".git").exists():
        print(
            f"error: Chromium src does not look like a git checkout: "
            f"{chromium_src}",
            file=sys.stderr,
        )
        return 2

    if not args.skip_uc:
        uc_series_file = UC_ROOT / "patches" / "series"
        uc_patches_dir = UC_ROOT / "patches"
        uc_series = read_series(uc_series_file)
        if not uc_series:
            print(
                f"warning: UC series file is empty or missing: "
                f"{uc_series_file}",
                file=sys.stderr,
            )
        for entry in uc_series:
            apply_one(uc_patches_dir / entry, chromium_src)

    ghostium_series_file = GHOSTIUM_PATCHES_DIR / "series"
    for entry in read_series(ghostium_series_file):
        apply_one(GHOSTIUM_PATCHES_DIR / entry, chromium_src)

    print("[apply_patches] all patches applied successfully.")
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except subprocess.CalledProcessError as exc:
        print(f"[apply_patches] failed: {exc}", file=sys.stderr)
        sys.exit(exc.returncode or 1)
    except FileNotFoundError as exc:
        print(f"[apply_patches] {exc}", file=sys.stderr)
        sys.exit(2)
