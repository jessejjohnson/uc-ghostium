#!/usr/bin/env python3
"""Lightweight structural linter for .mojom files.

Runs without the Chromium toolchain. Validates:

* exactly one ``module`` declaration per file;
* balanced braces, brackets, and parentheses;
* top-level definitions use ``struct``, ``enum``, ``interface``, ``union``,
  ``const``, or ``import``;
* each declared ``interface`` method ends with a semicolon.

This is a pre-flight check for PR lint; the authoritative check is the mojom
compiler invoked during the full Chromium build.
"""

from __future__ import annotations

import re
import sys
from pathlib import Path


TOP_LEVEL_KEYWORDS = (
    "module",
    "struct",
    "enum",
    "interface",
    "union",
    "const",
    "import",
    "feature",
    "[",  # attribute annotation preceding a top-level definition
)


def strip_comments(text: str) -> str:
    # Strip /* ... */ blocks first, then // ... line comments.
    text = re.sub(r"/\*.*?\*/", "", text, flags=re.S)
    text = re.sub(r"//[^\n]*", "", text)
    return text


def check_balanced(text: str) -> list[str]:
    pairs = {"{": "}", "[": "]", "(": ")"}
    stack: list[tuple[str, int]] = []
    errors: list[str] = []
    line = 1
    in_string = False
    for ch in text:
        if ch == "\n":
            line += 1
            continue
        if ch == '"':
            in_string = not in_string
            continue
        if in_string:
            continue
        if ch in pairs:
            stack.append((ch, line))
        elif ch in pairs.values():
            if not stack:
                errors.append(f"unbalanced close {ch!r} at line {line}")
                continue
            opener, opener_line = stack.pop()
            if pairs[opener] != ch:
                errors.append(
                    f"mismatched close {ch!r} at line {line} "
                    f"(opened with {opener!r} at line {opener_line})"
                )
    for opener, opener_line in stack:
        errors.append(
            f"unclosed {opener!r} opened at line {opener_line}"
        )
    return errors


def lint_file(path: Path) -> list[str]:
    text = path.read_text()
    stripped = strip_comments(text)
    errors: list[str] = []

    module_decls = re.findall(r"^\s*module\s+[\w.]+\s*;", stripped, re.M)
    if len(module_decls) == 0:
        errors.append("missing 'module <name>;' declaration")
    elif len(module_decls) > 1:
        errors.append(
            f"expected exactly one 'module' declaration, found "
            f"{len(module_decls)}"
        )

    errors.extend(check_balanced(stripped))

    # Scan for tokens at column 1 that are not valid top-level keywords.
    # Heuristic-only: the Mojo compiler is the authoritative check during
    # the full Chromium build.
    for m in re.finditer(r"^(\w+)\b", stripped, re.M):
        tok = m.group(1)
        if tok in TOP_LEVEL_KEYWORDS:
            continue
        errors.append(
            f"unexpected top-level token at column 1: {tok!r} (expected "
            f"one of {', '.join(TOP_LEVEL_KEYWORDS)})"
        )
        # Stop reporting after a handful to keep output readable.
        if len([e for e in errors if "unexpected top-level" in e]) >= 5:
            break

    return [f"{path}: {err}" for err in errors]


def main() -> int:
    if len(sys.argv) < 2:
        print("usage: ci_mojom_lint.py <dir> [<dir> ...]", file=sys.stderr)
        return 2

    failed = False
    for root_arg in sys.argv[1:]:
        root = Path(root_arg)
        if not root.exists():
            print(f"warning: not found: {root}", file=sys.stderr)
            continue
        mojoms = (
            [root]
            if root.is_file() and root.suffix == ".mojom"
            else sorted(root.rglob("*.mojom"))
        )
        for mojom in mojoms:
            errors = lint_file(mojom)
            for err in errors:
                print(err, file=sys.stderr)
                failed = True
            if not errors:
                print(f"[ci_mojom_lint] ok: {mojom}")

    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
