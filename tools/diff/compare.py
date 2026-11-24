#!/usr/bin/env python3
# SPDX-License-Identifier: AGPL-3.0-only
"""Compare two dumps of numbers line by line:   tools/diff/compare.py a.txt b.txt [--rtol 1e-9] [--atol 0]

Every line is split at blanks, commas and semicolons.  A field that is a number on both sides is
compared numerically (|a - b| <= atol + rtol * max(|a|, |b|)); any other field must be the same text.
Prints the first differences and the largest difference seen; the exit status is 0 when the files agree.
"""
import argparse
import math
import re
import sys


def number(text):
    try:
        return float(text)
    except ValueError:
        return None


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("a")
    ap.add_argument("b")
    ap.add_argument("--rtol", type=float, default=1e-9, help="relative tolerance (default 1e-9)")
    ap.add_argument("--atol", type=float, default=0.0, help="absolute tolerance (default 0)")
    ap.add_argument("--show", type=int, default=10, help="how many differences to print (default 10)")
    args = ap.parse_args()

    with open(args.a) as f:
        la = f.read().splitlines()
    with open(args.b) as f:
        lb = f.read().splitlines()
    split = re.compile(r"[\s,;]+")
    bad = 0
    numbers = 0
    worst = (0.0, 0, "", "")  # relative difference, line, a, b
    for n, (x, y) in enumerate(zip(la, lb), 1):
        fx = [t for t in split.split(x.strip()) if t]
        fy = [t for t in split.split(y.strip()) if t]
        same = len(fx) == len(fy)
        if same:
            for tx, ty in zip(fx, fy):
                vx, vy = number(tx), number(ty)
                if vx is None or vy is None:
                    same = same and tx == ty
                    continue
                numbers += 1
                if vx == vy or (math.isnan(vx) and math.isnan(vy)):
                    continue
                d = abs(vx - vy)
                rel = d / max(abs(vx), abs(vy)) if math.isfinite(d) else math.inf
                if rel > worst[0]:
                    worst = (rel, n, tx, ty)
                if not d <= args.atol + args.rtol * max(abs(vx), abs(vy)):
                    same = False
        if not same:
            bad += 1
            if bad <= args.show:
                print(f"line {n}:\n  a: {x}\n  b: {y}")
    if len(la) != len(lb):
        print(f"{args.a} has {len(la)} lines, {args.b} has {len(lb)}")
    print(f"{min(len(la), len(lb))} lines, {numbers} numbers, {bad} lines differ; "
          f"largest relative difference {worst[0]:.3g}" + (f" (line {worst[1]}: {worst[2]} / {worst[3]})" if worst[0] else ""))
    return 0 if bad == 0 and len(la) == len(lb) else 1


if __name__ == "__main__":
    sys.exit(main())
