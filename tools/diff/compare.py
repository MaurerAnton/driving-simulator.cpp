#!/usr/bin/env python3
# SPDX-License-Identifier: AGPL-3.0-only
"""Compare two dumps of numbers line by line:   tools/diff/compare.py a.txt b.txt [--rel 1e-9] [--abs 1e-12]

A line is words separated by blanks.  Two words that both read as numbers are alike
when they differ by no more than `--abs`, or by no more than `--rel` of the larger of
the two (NaN is like NaN, an infinity like the same infinity); other words must be
the same text.  Prints what was compared, the largest difference of two numbers and
the largest relative difference (of those that differ by more than `--abs`), and the
first `--show` lines that differ.  Exit status 1 when anything differs."""
import argparse
import math
import sys


def number(word):
    try:
        return float(word)
    except ValueError:
        return None


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("a")
    ap.add_argument("b")
    ap.add_argument("--rel", type=float, default=1e-9, help="relative tolerance (default 1e-9)")
    ap.add_argument("--abs", type=float, default=1e-12, dest="abs_", help="absolute tolerance (default 1e-12)")
    ap.add_argument("--show", type=int, default=20, help="how many differing lines to print (default 20)")
    args = ap.parse_args()

    with open(args.a, encoding="utf-8") as f:
        la = f.read().splitlines()
    with open(args.b, encoding="utf-8") as f:
        lb = f.read().splitlines()

    bad = 0
    numbers = 0
    words = 0
    max_rel = 0.0
    max_abs = 0.0
    worst = ""
    for i in range(max(len(la), len(lb))):
        if i >= len(la) or i >= len(lb):
            bad += 1
            if bad <= args.show:
                print(f"line {i + 1}: only in {args.b if i >= len(la) else args.a}: {(lb if i >= len(la) else la)[i][:200]}")
            continue
        wa, wb = la[i].split(), lb[i].split()
        ok = len(wa) == len(wb)
        why = "" if ok else f"{len(wa)} words against {len(wb)}"
        for k in range(min(len(wa), len(wb))):
            if wa[k] == wb[k]:
                if number(wa[k]) is not None:
                    numbers += 1
                else:
                    words += 1
                continue
            x, y = number(wa[k]), number(wb[k])
            if x is None or y is None:
                ok = False
                why = why or f"word {k + 1}: {wa[k]} against {wb[k]}"
                continue
            numbers += 1
            if math.isnan(x) and math.isnan(y):
                continue
            if math.isnan(x) or math.isnan(y) or math.isinf(x) or math.isinf(y):
                if x != y:
                    ok = False
                    why = why or f"word {k + 1}: {wa[k]} against {wb[k]}"
                continue
            d = abs(x - y)
            scale = max(abs(x), abs(y))
            rel = d / scale if scale > 0 else 0.0
            if d > max_abs:
                max_abs = d
            if d > args.abs_ and rel > max_rel:
                max_rel = rel
                worst = f"line {i + 1} ({wa[0]}): {wa[k]} against {wb[k]}"
            if d <= args.abs_ or rel <= args.rel:
                continue
            ok = False
            why = why or f"word {k + 1}: {wa[k]} against {wb[k]} (differ by {d:.3g}, {rel:.3g} of it)"
        if not ok:
            bad += 1
            if bad <= args.show:
                print(f"line {i + 1}: {why}\n   a: {la[i][:300]}\n   b: {lb[i][:300]}")

    print(f"{max(len(la), len(lb))} lines, {numbers} numbers and {words} other words compared; "
          f"largest difference {max_abs:.3g}, largest relative difference {max_rel:.3g} (of those above {args.abs_:g})"
          + (f"; worst: {worst}" if worst else ""))
    if bad:
        print(f"{bad} lines differ")
        return 1
    print("the same")
    return 0


if __name__ == "__main__":
    sys.exit(main())
