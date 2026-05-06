#!/usr/bin/env python3
"""CI helper: structural dry-run of UC's and Ghostium's patch series.

On a runner without a full Chromium checkout we cannot truly apply the
patches, so this script instead verifies the structural invariants that the
lint job cares about:

* every entry in each series file corresponds to an existing patch file;
* every patch file is syntactically valid unified-diff (``git apply
  --numstat`` succeeds);
* Ghostium series is in ascending ``NNNN-`` prefix order.

If ``CHROMIUM_SRC`` points at a real Chromium checkout the script also runs
``git apply --check`` on each patch, which is the strongest signal CI can
produce without a full build.
"""

from __future__ import annotations

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


def structural_check(patch_path: Path) -> list[str]:
    errors: list[str] = []
    if not patch_path.is_file():
        errors.append(f"missing file: {patch_path}")
        return errors
    try:
        subprocess.run(
            ["git", "apply", "--numstat", str(patch_path)],
            cwd=GHOSTIUM_ROOT,
            check=True,
            capture_output=True,
        )
    except subprocess.CalledProcessError as exc:
        errors.append(
            f"git apply --numstat failed for {patch_path}: "
            f"{exc.stderr.decode(errors='replace').strip()}"
        )
    return errors


def check_ascending(entries: list[str]) -> list[str]:
    errors: list[str] = []
    last = -1
    for entry in entries:
        prefix = entry.split("-", 1)[0]
        try:
            n = int(prefix)
        except ValueError:
            errors.append(f"bad series entry (no NNNN- prefix): {entry}")
            continue
        if n <= last:
            errors.append(
                f"series out of order: {entry} (n={n}, last={last})"
            )
        last = n
    return errors


def main() -> int:
    failed = False

    ghostium_series = read_series(GHOSTIUM_PATCHES_DIR / "series")
    for err in check_ascending(ghostium_series):
        print(f"[ci_patch_dryrun] {err}", file=sys.stderr)
        failed = True

    for entry in ghostium_series:
        for err in structural_check(GHOSTIUM_PATCHES_DIR / entry):
            print(f"[ci_patch_dryrun] {err}", file=sys.stderr)
            failed = True

    uc_series_file = UC_ROOT / "patches" / "series"
    if uc_series_file.is_file():
        uc_series = read_series(uc_series_file)
        uc_patches_dir = UC_ROOT / "patches"
        for entry in uc_series:
            patch_path = uc_patches_dir / entry
            if not patch_path.is_file():
                print(
                    f"[ci_patch_dryrun] UC patch missing: {patch_path}",
                    file=sys.stderr,
                )
                failed = True

    chromium_src = os.environ.get("CHROMIUM_SRC")
    if chromium_src and Path(chromium_src, ".git").exists():
        print(
            "[ci_patch_dryrun] CHROMIUM_SRC set; running git apply --check"
        )
        for entry in ghostium_series:
            patch_path = GHOSTIUM_PATCHES_DIR / entry
            res = subprocess.run(
                ["git", "apply", "--check", str(patch_path)],
                cwd=chromium_src,
                capture_output=True,
            )
            if res.returncode != 0:
                print(
                    f"[ci_patch_dryrun] git apply --check failed for "
                    f"{patch_path.name}: "
                    f"{res.stderr.decode(errors='replace').strip()}",
                    file=sys.stderr,
                )
                failed = True
    else:
        print(
            "[ci_patch_dryrun] CHROMIUM_SRC not available; skipping real "
            "apply check.",
            file=sys.stderr,
        )

    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
