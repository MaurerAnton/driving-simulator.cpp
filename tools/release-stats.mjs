// SPDX-License-Identifier: AGPL-3.0-only
// How big the game is at a version – the `size` of its entry in src/game/releases.h, counted as the original's
// tools/release-stats.ts counts (lines of text files, by kind; bytes of all files), for the port's layout:
//   code: src/ and tools/ (.cpp .h .inc .js .mjs .css .html .py)    tests: tests/    i18n: web/lang/, i18n-extra/,
//   src/i18n/builtin_*.inc    maps: not counted – 2.0 reads 1.7.7's map-assets, which are not in this repository.
//   node tools/release-stats.mjs            the files as they are committed (git ls-files), as a C++ literal
import { execFileSync } from 'node:child_process';
import { readFileSync, statSync } from 'node:fs';

const CODE_EXT = /\.(cpp|h|inc|js|mjs|css|html|py)$/;
const kindOf = (p) => {
  if (p.startsWith('web/lang/') || p.startsWith('i18n-extra/') || /^src\/i18n\/builtin_[^/]+\.inc$/.test(p)) return 'i18n';
  if (!CODE_EXT.test(p)) return '';
  if (p.startsWith('tests/')) return 'tests';
  if (p.startsWith('src/') || p.startsWith('tools/')) return 'code';
  return '';
};
const s = { lines: 0, code: 0, tests: 0, i18n: 0, bytes: 0 };
for (const p of execFileSync('git', ['ls-files', '-z']).toString().split('\0').filter(Boolean)) {
  let st;
  try {
    st = statSync(p);
  } catch {
    continue;
  }
  if (!st.isFile()) continue;
  s.bytes += st.size;
  const data = readFileSync(p);
  if (data.subarray(0, 8000).includes(0)) continue;
  let n = 0;
  for (const b of data) if (b === 10) n++;
  if (data.length > 0 && data[data.length - 1] !== 10) n++;
  s.lines += n;
  const k = kindOf(p);
  if (k) s[k] += n;
}
console.log(`.size = ReleaseSize{.lines = ${s.lines}, .code = ${s.code}, .tests = ${s.tests}, .i18n = ${s.i18n}, .bytes = ${s.bytes}},`);
