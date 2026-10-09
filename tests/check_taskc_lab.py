#!/usr/bin/env python3
"""Regression guard for the Task C experiment lab.

Fails on unresolved merge-conflict markers or syntax errors in
experiments/task-c/run.py so breakage is caught by `ctest` before anyone
runs the experiment. Standard library only; takes an optional path argument
(defaults to the lab file) to allow checking historical versions.
"""

import ast
import sys
from pathlib import Path

LAB_RELATIVE = Path("experiments") / "task-c" / "run.py"


def check(lab):
    """Return a list of failure descriptions (empty means the lab is runnable)."""
    failures = []
    try:
        text = lab.read_text(encoding="utf-8")
    except OSError as error:
        return [f"{lab}: cannot read lab file: {error}"]
    for number, line in enumerate(text.splitlines(), start=1):
        stripped = line.strip()
        # A bare '=======' line or a '<<<<<<<'/'>>>>>>>'-prefixed line can
        # never be valid Python outside a merge conflict; anchoring avoids
        # false positives on ordinary shift/comparison operators.
        if stripped.startswith(("<<<<<<<", ">>>>>>>")) or stripped == "=======":
            failures.append(f"{lab}:{number}: unresolved merge marker: {stripped[:32]!r}")
    try:
        ast.parse(text, filename=str(lab))
    except SyntaxError as error:
        failures.append(f"{lab}:{error.lineno}: syntax error: {error.msg}")
    return failures


def main(argv):
    lab = Path(argv[1]) if len(argv) > 1 else Path(__file__).resolve().parents[1] / LAB_RELATIVE
    failures = check(lab)
    if failures:
        for failure in failures:
            print(f"check_taskc_lab: {failure}", file=sys.stderr)
        return 1
    print(f"check_taskc_lab: {lab} is runnable (no markers, syntax OK)")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
