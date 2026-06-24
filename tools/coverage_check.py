#!/usr/bin/env python3
"""Regression guard: assert the test matrix is complete so a missing category
can never silently pass as green. Fails (non-zero) unless every operation has a
shape test at each field width AND a differential kernel -- so adding an op
means dropping in both files, never editing a harness.

Usage: coverage_check.py SHAPE_DIR DIFF_DIR
"""
import os
import re
import sys

WIDTHS = [1, 2, 4]
SHAPE_RE = re.compile(r"^([a-z]+)_i([124])\.ll$")


def main(shape_dir, diff_dir):
    shape, errs = {}, []
    for f in os.listdir(shape_dir):
        m = SHAPE_RE.match(f)
        if m:
            shape.setdefault(m.group(1), set()).add(int(m.group(2)))
    diff = {f[:-3] for f in os.listdir(diff_dir) if f.endswith(".ll")}

    ops = set(shape) | diff
    if not ops:
        errs.append("no operations found at all")
    for op in sorted(ops):
        missing = [w for w in WIDTHS if w not in shape.get(op, set())]
        if missing:
            errs.append("%s: missing shape test(s) for i%s"
                        % (op, ",i".join(map(str, missing))))
        if op not in diff:
            errs.append("%s: missing differential test" % op)

    if errs:
        for e in errs:
            print("MISSING:", e)
        return 1
    print("COVERAGE OK (%d ops x %d widths, shape + differential)"
          % (len(ops), len(WIDTHS)))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1], sys.argv[2]))
