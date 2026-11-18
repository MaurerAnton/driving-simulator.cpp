#!/usr/bin/env python3
# SPDX-License-Identifier: AGPL-3.0-only
"""LOC parity report: TypeScript original vs C++ port.

The port tracks upstream versions (v0.1.0 … v1.6.0) as a measured ratio:
  parity = C++ LOC / TS LOC   (target ~1.0 for the final version).

TS reference line counts are taken from the read-only source repo when it is
available (env FAHRSIM_TS_REPO, default ../driving-simulator); otherwise the
numbers recorded in versions.tsv are used, so the report works anywhere.

Usage:  python3 tools/parity.py [--version 1.6.0]
"""
from __future__ import annotations

import os
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
TS_REPO = Path(os.environ.get("FAHRSIM_TS_REPO", ROOT.parent / "driving-simulator"))
VERSIONS = ROOT / "tools" / "versions.tsv"


def count_lines(paths) -> int:
    total = 0
    for p in paths:
        try:
            total += sum(1 for _ in open(p, encoding="utf-8", errors="replace"))
        except OSError:
            pass
    return total


def ts_loc_at(sha: str, repo: Path) -> int:
    """Lines of src/*.ts at a given upstream commit (read-only)."""
    try:
        out = subprocess.run(
            ["git", "-C", str(repo), "ls-tree", "-r", "--name-only", sha, "--", "src"],
            capture_output=True, text=True, check=True,
        ).stdout.splitlines()
        files = [f for f in out if f.endswith(".ts")]
        if not files:
            return 0
        total = 0
        for f in files:
            blob = subprocess.run(
                ["git", "-C", str(repo), "show", f"{sha}:{f}"],
                capture_output=True, text=True, check=True,
            ).stdout
            total += blob.count("\n")
        return total
    except (subprocess.CalledProcessError, OSError):
        return 0


def read_versions() -> list[tuple[str, str, int]]:
    rows = []
    if not VERSIONS.exists():
        return rows
    for line in VERSIONS.read_text(encoding="utf-8").splitlines():
        line = line.strip()
        if not line or line.startswith("#"):
            continue
        ver, sha, ts = line.split("\t")
        rows.append((ver, sha, int(ts)))
    return rows


def main() -> int:
    want = None
    if len(sys.argv) > 1 and sys.argv[1] == "--version":
        want = sys.argv[2] if len(sys.argv) > 2 else None

    cpp_files = list((ROOT / "src").rglob("*.cpp")) + list((ROOT / "src").rglob("*.h")) \
        + list((ROOT / "src").rglob("*.hpp"))
    cpp_loc = count_lines(cpp_files)

    rows = read_versions()
    has_repo = TS_REPO.exists()
    print(f"C++ port: {len(cpp_files)} files, {cpp_loc} LOC")
    print(f"TS reference repo: {TS_REPO} ({'available' if has_repo else 'missing, using recorded numbers'})")
    print()
    print(f"{'version':<9} {'sha':<12} {'TS LOC':>9} {'C++ LOC':>9} {'parity':>8}")
    for ver, sha, ts in rows:
        if has_repo:
            measured = ts_loc_at(sha, TS_REPO)
            if measured:
                ts = measured
        parity = (cpp_loc / ts) if ts else 0
        mark = ""
        if ver == (want or "1.6.0"):
            mark = "  <- target"
        print(f"{ver:<9} {sha[:10]:<12} {ts:>9} {cpp_loc:>9} {parity:>8.2f}{mark}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
