#!/usr/bin/env python3
# SPDX-License-Identifier: AGPL-3.0-only
"""Compare what the original printed with what the port printed.

    tools/diff/compare.py original.txt port.txt [--rtol 1e-9] [--atol 1e-9] [--show 20]

Both files hold lines of a name followed by values (tools/diff/<name>.ts and
tools/dev/<name>_dump.cpp write them).  Line by line, value by value:

  - whole numbers (counts, ids) and words must be the same;
  - fractions must agree to a relative `rtol` – or, for numbers near zero, to an
    absolute `atol` (a difference of a nanometre between two offsets of 1e-17 m
    is none).

Prints how much was compared, how many fractions were the same to the last
digit, the largest difference seen, and the first lines that differ.  The exit
status is 0 if nothing differs.
"""
import argparse
import math
import re
import sys

WHOLE = re.compile(r'^-?\d+$')


def number(text):
    try:
        return float(text)
    except ValueError:
        return None


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('original')
    ap.add_argument('port')
    ap.add_argument('--rtol', type=float, default=1e-9, help='relative tolerance for fractions (default 1e-9)')
    ap.add_argument('--atol', type=float, default=1e-9, help='absolute tolerance for fractions near zero (default 1e-9)')
    ap.add_argument('--show', type=int, default=20, help='how many differing lines to print (default 20)')
    args = ap.parse_args()

    with open(args.original, encoding='utf-8') as f:
        a_lines = f.read().split('\n')
    with open(args.port, encoding='utf-8') as f:
        b_lines = f.read().split('\n')
    while a_lines and a_lines[-1] == '':
        a_lines.pop()
    while b_lines and b_lines[-1] == '':
        b_lines.pop()

    bad = 0          # lines that differ
    shown = 0
    values = 0       # values compared
    fractions = 0    # of them fractions
    same_text = 0    # fractions that are the same to the last digit
    worst = 0.0      # the largest relative difference among fractions that pass
    worst_at = None
    worst_abs = 0.0  # the largest absolute difference among those that pass only for being near zero
    worst_abs_at = None
    by_name = {}     # per line name: [lines, lines that differ]

    def report(i, why, la, lb):
        nonlocal shown
        if shown < args.show:
            print(f'line {i + 1}: {why}\n  original: {la[:400]}\n  port:     {lb[:400]}')
            shown += 1

    n = min(len(a_lines), len(b_lines))
    for i in range(n):
        la, lb = a_lines[i], b_lines[i]
        ta, tb = la.split(' '), lb.split(' ')
        name = ta[0]
        stat = by_name.setdefault(name, [0, 0])
        stat[0] += 1
        if la == lb:
            for t in ta[1:]:
                values += 1
                if not WHOLE.match(t) and number(t) is not None:
                    fractions += 1
                    same_text += 1
            continue
        why = None
        if len(ta) != len(tb):
            why = f'{len(ta) - 1} values against {len(tb) - 1}'
        else:
            for k, (x, y) in enumerate(zip(ta, tb)):
                if k == 0:
                    if x != y:
                        why = f'"{x}" against "{y}"'
                        break
                    continue
                values += 1
                if x == y:
                    if not WHOLE.match(x) and number(x) is not None:
                        fractions += 1
                        same_text += 1
                    continue
                fx, fy = number(x), number(y)
                if fx is None or fy is None or (WHOLE.match(x) and WHOLE.match(y)):
                    why = f'value {k}: {x} against {y}'
                    break
                fractions += 1
                if math.isnan(fx) or math.isnan(fy) or math.isinf(fx) or math.isinf(fy):
                    why = f'value {k}: {x} against {y}'
                    break
                d = abs(fx - fy)
                rel = d / max(abs(fx), abs(fy))
                if rel <= args.rtol:
                    if rel > worst:
                        worst = rel
                        worst_at = (i + 1, k, x, y)
                    continue
                if d <= args.atol:
                    # (two numbers near zero: the relative difference says nothing)
                    if d > worst_abs:
                        worst_abs = d
                        worst_abs_at = (i + 1, k, x, y)
                    continue
                why = f'value {k}: {x} against {y} (differs by {d:.3g}, relative {rel:.3g})'
                break
        if why:
            bad += 1
            stat[1] += 1
            report(i, why, la, lb)

    if len(a_lines) != len(b_lines):
        bad += 1
        print(f'{len(a_lines)} lines in the original, {len(b_lines)} in the port')
        longer = a_lines if len(a_lines) > len(b_lines) else b_lines
        print(f'  the first line that has no partner: {longer[n][:300]}')

    print(f'{n} lines, {values} values compared; {fractions} of them fractions, {same_text} the same to the last digit'
          f' ({fractions - same_text} within tolerance)')
    if worst_at:
        print(f'largest difference that passes: relative {worst:.3g} (line {worst_at[0]}, value {worst_at[1]}: {worst_at[2]} / {worst_at[3]})')
    if worst_abs_at:
        print(f'largest difference near zero that passes: {worst_abs:.3g} (line {worst_abs_at[0]}, value {worst_abs_at[1]}: {worst_abs_at[2]} / {worst_abs_at[3]})')
    kinds = ', '.join(f'{name} {c[0]}' + (f' ({c[1]} differ)' if c[1] else '') for name, c in sorted(by_name.items()))
    print(f'lines by name: {kinds}')
    if bad:
        print(f'{bad} lines DIFFER')
        return 1
    print('the outputs agree')
    return 0


if __name__ == '__main__':
    sys.exit(main())
