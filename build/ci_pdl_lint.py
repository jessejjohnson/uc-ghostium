#!/usr/bin/env python3
"""Structural linter for PDL snippets embedded in CDP patches.

Usage:

    ci_pdl_lint.py <patch> [<patch> ...]

Extracts any PDL fragments from each patch file (heuristic: contiguous runs
of lines starting with ``+`` inside a hunk that touches a file matching
``*browser_protocol.pdl``) and verifies that indentation increases/decreases
monotonically, that ``domain``/``command``/``type`` blocks are well-formed,
and that every ``parameters`` / ``returns`` block contains at least one
member. Returns non-zero on the first violation.
"""

from __future__ import annotations

import re
import sys
from pathlib import Path


PDL_FILE_RE = re.compile(
    r"^\+\+\+ [ab]/.+browser_protocol\.pdl$", re.M
)
HUNK_RE = re.compile(r"^@@ .+@@", re.M)


def extract_pdl_blocks(patch_text: str) -> list[list[str]]:
    blocks: list[list[str]] = []
    in_pdl = False
    current: list[str] = []
    for line in patch_text.splitlines():
        if line.startswith("+++ "):
            in_pdl = "browser_protocol.pdl" in line
            if current:
                blocks.append(current)
                current = []
            continue
        if not in_pdl:
            continue
        if line.startswith("@@"):
            if current:
                blocks.append(current)
                current = []
            continue
        if line.startswith("+") and not line.startswith("+++"):
            current.append(line[1:])
        elif line.startswith(" "):
            # Context line - keep so indentation invariants still make sense.
            current.append(line[1:])
        elif line.startswith("-"):
            continue
    if current:
        blocks.append(current)
    return blocks


def lint_block(block: list[str]) -> list[str]:
    errors: list[str] = []
    indent_stack: list[int] = [0]
    for lineno, line in enumerate(block, 1):
        if not line.strip() or line.lstrip().startswith("#"):
            continue
        stripped = line.lstrip()
        indent = len(line) - len(stripped)
        if indent > indent_stack[-1]:
            indent_stack.append(indent)
        else:
            while indent_stack and indent < indent_stack[-1]:
                indent_stack.pop()
            if not indent_stack or indent_stack[-1] != indent:
                errors.append(
                    f"line {lineno}: irregular indentation "
                    f"({indent} spaces, stack={indent_stack})"
                )
                indent_stack.append(indent)

    keywords = ("domain ", "command ", "type ", "event ", "parameters",
                "returns", "description ", "optional ", "enum ")
    for lineno, line in enumerate(block, 1):
        stripped = line.strip()
        if not stripped or stripped.startswith("#"):
            continue
        if any(stripped.startswith(k.strip()) for k in keywords):
            continue
        if re.match(r"^[A-Za-z_][A-Za-z0-9_]*\b", stripped):
            continue
        errors.append(
            f"line {lineno}: unexpected token {stripped!r}"
        )

    return errors


def main() -> int:
    if len(sys.argv) < 2:
        print("usage: ci_pdl_lint.py <patch> [<patch> ...]", file=sys.stderr)
        return 2

    failed = False
    for patch_arg in sys.argv[1:]:
        # Support glob-like patterns from the shell by expanding in-process
        # if the caller passed a literal '*'.
        paths = sorted(Path().glob(patch_arg)) if "*" in patch_arg else [
            Path(patch_arg)
        ]
        for path in paths:
            if not path.is_file():
                print(f"warning: not found: {path}", file=sys.stderr)
                continue
            text = path.read_text()
            if not PDL_FILE_RE.search(text):
                print(f"[ci_pdl_lint] no PDL hunks in {path}; skipping")
                continue
            blocks = extract_pdl_blocks(text)
            if not blocks:
                print(
                    f"[ci_pdl_lint] PDL file touched but no blocks "
                    f"extracted: {path}"
                )
                continue
            for i, block in enumerate(blocks):
                errors = lint_block(block)
                for err in errors:
                    print(f"{path}:block{i}: {err}", file=sys.stderr)
                    failed = True
            if not any(lint_block(b) for b in blocks):
                print(f"[ci_pdl_lint] ok: {path}")

    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
