#!/usr/bin/env node
// SPDX-License-Identifier: AGPL-3.0-only
/**
 * The texts of the original, made into the data the port ships and builds in.
 *
 *   node tools/i18n-export.mjs [original]            write the texts
 *   node tools/i18n-export.mjs --check [original]    write nothing: say what would change (exit 1 if anything would)
 *   node tools/i18n-export.mjs --unicode             src/i18n/unicode.inc from this node's Unicode data
 *   node tools/i18n-export.mjs --intl                the CLDR table in src/platform/native/intl_native.cpp from this node's ICU
 *
 * `original` is the path of the driving-simulator repository (default /home/user/driving-simulator).
 * Its dictionaries are read by importing them (node runs the .ts files as they are), so what is
 * written is exactly what the original's program sees:
 *
 *   web/lang/<code>.json        the texts of a language: one flat object key → text, in the order of
 *                               the language's own file (Object.keys), one entry on a line
 *   web/lang/exam/<code>.json   the words of the learning modes (src/i18n/exam/<code>.ts) in the shape
 *                               they have there: { questions, danger, signs }
 *   web/lang/index.json         what is known before a language is loaded: the table LANGS of
 *                               src/i18n/index.ts, the languages of the learning modes, and a stamp –
 *                               a short hash of all these files, which the program puts into the
 *                               addresses it fetches (lang/fr.json?v=<stamp>), so that a browser never
 *                               mixes the texts of two builds
 *   src/i18n/builtin_<code>.inc the languages without a loader in LANGS (German and English) once more
 *   src/i18n/exam/builtin_en.inc  and the English words of the learning modes: the same JSON as one
 *   src/i18n/builtin.cpp        raw string each, compiled into the program, with LANGS and the stamp
 *
 * Run it again whenever the original's texts change, and commit what it writes: the same original
 * always gives the same bytes (no dates, no machine's order of anything).  Files of languages that
 * are no longer there are removed.
 *
 * The two other jobs do not depend on the original, only on the node that runs them (its Unicode and
 * CLDR versions are written into what they make): the classes of characters the regular expressions
 * of speechtext.ts and keys.ts ask for (\p{L}, \p{Script=Latin}, toUpperCase …), and the separators,
 * units and list words of the game's languages for the native host's Intl.  They are needed again
 * only when the original asks for another class, unit or language.
 */
import { createHash } from 'node:crypto';
import { existsSync, mkdirSync, readFileSync, readdirSync, rmSync, writeFileSync } from 'node:fs';
import { dirname, join, relative, resolve } from 'node:path';
import { fileURLToPath, pathToFileURL } from 'node:url';
import { isDeepStrictEqual } from 'node:util';
import { gzipSync } from 'node:zlib';

const ROOT = resolve(dirname(fileURLToPath(import.meta.url)), '..');
const args = process.argv.slice(2);
const flags = new Set(args.filter((a) => a.startsWith('--')));
const paths = args.filter((a) => !a.startsWith('--'));
const CHECK = flags.has('--check');
for (const f of flags) {
  if (!['--check', '--unicode', '--intl'].includes(f)) {
    console.error(`unknown option ${f}`);
    process.exit(2);
  }
}

/** The files to write: path below the repository → text. */
const out = new Map();

const fail = (message) => {
  console.error(`i18n-export: ${message}`);
  process.exit(1);
};

// ------------------------------------------------------------------ the texts

/** JSON with the members of the outer `depth` levels of objects on lines of their own (texts are compared line by line). */
function jsonLines(value, depth) {
  if (depth > 0 && value !== null && typeof value === 'object' && !Array.isArray(value)) {
    const keys = Object.keys(value);
    if (keys.length === 0) return '{}';
    return `{\n${keys.map((k) => `${JSON.stringify(k)}:${jsonLines(value[k], depth - 1)}`).join(',\n')}\n}`;
  }
  return JSON.stringify(value);
}

/** Every text in a value (for the checks). */
function* stringsOf(value, where) {
  if (typeof value === 'string') yield [where, value];
  else if (Array.isArray(value)) for (let i = 0; i < value.length; i++) yield* stringsOf(value[i], `${where}[${i}]`);
  else if (value !== null && typeof value === 'object') {
    for (const [k, v] of Object.entries(value)) {
      yield [`${where} (key)`, k];
      yield* stringsOf(v, `${where}.${k}`);
    }
  }
}

/** What must hold for a file to be read back as it was: plain data, whole characters, nothing a C++ string ends at. */
function checkData(value, name, text) {
  if (!isDeepStrictEqual(JSON.parse(text), value)) fail(`${name}: not plain data (it does not come back from JSON as it went in)`);
  for (const [where, s] of stringsOf(value, name)) {
    if (!s.isWellFormed()) fail(`${where}: half a character (a lone surrogate)`);
    if (s.includes('\0')) fail(`${where}: contains U+0000`);
  }
}

const RAW = 'FSLANG';
/** The JSON as a C++ raw string literal. */
function rawString(json, name) {
  if (json.includes(`)${RAW}"`)) fail(`${name}: contains the end of the raw string`);
  return `R"${RAW}(${json})${RAW}"`;
}

/** A C++ string literal (UTF-8 as it is). */
const cString = (s) => `"${[...s].map((ch) => (ch === '"' || ch === '\\' ? `\\${ch}` : ch.codePointAt(0) < 0x20 || ch.codePointAt(0) === 0x7f ? `\\${ch.codePointAt(0).toString(8).padStart(3, '0')}` : ch)).join('')}"`;

async function exportTexts(original) {
  const src = join(original, 'src/i18n');
  if (!existsSync(join(src, 'index.ts'))) fail(`${original}: no src/i18n/index.ts there (give the path of the driving-simulator repository)`);
  const load = async (file) => import(pathToFileURL(join(src, file)).href);
  const version = JSON.parse(readFileSync(join(original, 'package.json'), 'utf8')).version;
  const banner = `GENERATED by tools/i18n-export.mjs from driving-simulator ${version} – do not edit (run the script again)`;

  // the languages, in the order of the menu
  const { LANGS } = await load('index.ts');
  const langs = LANGS.map((l) => {
    for (const f of ['code', 'name', 'locale']) if (typeof l[f] !== 'string' || l[f] === '') fail(`LANGS: an entry without ${f}`);
    if (!/^[a-z]{2,3}$/.test(l.code)) fail(`LANGS: "${l.code}" is no language code`);
    const info = { code: l.code, name: l.name, locale: l.locale };
    if (l.rtl === true) info.rtl = true;
    // (no loader: the language is part of the program)
    if (!l.load) info.builtin = true;
    return info;
  });
  if (new Set(langs.map((l) => l.code)).size !== langs.length) fail('LANGS: a language twice');
  if (!langs.some((l) => l.code === 'en' && l.builtin)) fail('LANGS: English must be there without a loader (every other language falls back to it)');

  // the texts of each
  const data = new Map();
  let keys = 0;
  for (const l of langs) {
    const dict = (await load(`${l.code}.ts`)).default;
    if (dict === null || typeof dict !== 'object' || Array.isArray(dict)) fail(`${l.code}.ts: does not export an object of texts`);
    for (const [k, v] of Object.entries(dict)) if (typeof v !== 'string') fail(`${l.code}.ts: ${k} is not a text`);
    const json = jsonLines(dict, 1);
    checkData(dict, `${l.code}.ts`, json);
    data.set(`web/lang/${l.code}.json`, `${json}\n`);
    if (l.builtin) out.set(`src/i18n/builtin_${l.code}.inc`, `// ${banner}: web/lang/${l.code}.json as one string.\n${rawString(json, `${l.code}.ts`)}\n`);
    if (l.code === 'en') keys = Object.keys(dict).length;
  }

  // the words of the learning modes
  const exam = await load('exam/index.ts');
  const examLangs = Object.keys(exam.EXAM_LANGS);
  if (!examLangs.includes('en')) fail('EXAM_LANGS: English must be there (every other language falls back to it)');
  for (const code of examLangs) {
    if (!/^[a-z]{2,3}$/.test(code)) fail(`EXAM_LANGS: "${code}" is no language code`);
    const texts = code === 'en' ? exam.EXAM_EN : (await exam.EXAM_LANGS[code]()).default;
    if (texts === null || typeof texts !== 'object' || Array.isArray(texts)) fail(`exam/${code}.ts: does not export an object`);
    const json = jsonLines(texts, 2);
    checkData(texts, `exam/${code}.ts`, json);
    data.set(`web/lang/exam/${code}.json`, `${json}\n`);
    if (code === 'en') out.set('src/i18n/exam/builtin_en.inc', `// ${banner}: web/lang/exam/en.json as one string.\n${rawString(json, 'exam/en.ts')}\n`);
  }

  // the stamp: every file of texts, by its path
  const hash = createHash('sha256');
  hash.update(`${JSON.stringify(langs)}\n${JSON.stringify(examLangs)}\n`);
  for (const path of [...data.keys()].sort()) {
    const text = data.get(path);
    hash.update(`${path}\n${Buffer.byteLength(text)}\n${text}`);
    out.set(path, text);
  }
  const stamp = hash.digest('hex').slice(0, 12);

  out.set('web/lang/index.json', `${['{', `"stamp":${JSON.stringify(stamp)},`, `"source":${JSON.stringify(`driving-simulator ${version}`)},`, `"keys":${keys},`, '"langs":[', langs.map((l) => JSON.stringify(l)).join(',\n'), '],', `"exam":${JSON.stringify(examLangs)}`, '}'].join('\n')}\n`);

  const builtin = langs.filter((l) => l.builtin);
  const id = (code) => code.toUpperCase();
  out.set(
    'src/i18n/builtin.cpp',
    `// SPDX-License-Identifier: AGPL-3.0-only
// ${banner}.
/**
 * What the program knows before any language is fetched (src/i18n/builtin.h): the table LANGS of
 * the original's src/i18n/index.ts, the languages of EXAM_LANGS (src/i18n/exam/index.ts), the
 * stamp of the texts in web/lang (web/lang/index.json has the same), and the texts that are part
 * of the program – the languages without a loader in LANGS, and the English words of the learning
 * modes – each as the JSON of its file in web/lang, read when it is first asked for.
 */
#include "i18n/builtin.h"

namespace fs::i18n {

const char BUILTIN_STAMP[] = ${cString(stamp)};
const char BUILTIN_SOURCE[] = ${cString(version)};

const BuiltinLang BUILTIN_LANGS[] = {
${langs.map((l) => `    {${cString(l.code)}, ${cString(l.name)}, ${cString(l.locale)}, ${l.rtl === true}, ${l.builtin === true}},`).join('\n')}
};
const size_t BUILTIN_LANG_COUNT = sizeof BUILTIN_LANGS / sizeof BUILTIN_LANGS[0];

const char* const BUILTIN_EXAM_LANGS[] = {
    ${examLangs.map((c) => cString(c)).join(', ')},
};
const size_t BUILTIN_EXAM_LANG_COUNT = sizeof BUILTIN_EXAM_LANGS / sizeof BUILTIN_EXAM_LANGS[0];

namespace {
${builtin.map((l) => `const char DICT_${id(l.code)}[] =\n#include "i18n/builtin_${l.code}.inc"\n    ;`).join('\n')}
const char EXAM_EN_JSON[] =
#include "i18n/exam/builtin_en.inc"
    ;
}  // namespace

const BuiltinText BUILTIN_DICTS[] = {
${builtin.map((l) => `    {${cString(l.code)}, DICT_${id(l.code)}, sizeof DICT_${id(l.code)} - 1},`).join('\n')}
};
const size_t BUILTIN_DICT_COUNT = sizeof BUILTIN_DICTS / sizeof BUILTIN_DICTS[0];

const BuiltinText BUILTIN_EXAM_EN = {"en", EXAM_EN_JSON, sizeof EXAM_EN_JSON - 1};

}  // namespace fs::i18n
`,
  );

  // what a language no longer there has left behind
  const stale = [];
  const sweep = (dir, pattern) => {
    const abs = join(ROOT, dir);
    if (!existsSync(abs)) return;
    for (const f of readdirSync(abs)) if (pattern.test(f) && !out.has(`${dir}/${f}`)) stale.push(`${dir}/${f}`);
  };
  sweep('web/lang', /^[a-z]{2,3}\.json$/);
  sweep('web/lang/exam', /^[a-z]{2,3}\.json$/);
  sweep('src/i18n', /^builtin_[a-z]{2,3}\.inc$/);
  sweep('src/i18n/exam', /^builtin_[a-z]{2,3}\.inc$/);

  // the sizes: as they lie on the disk, and as a server sends them
  const size = (filter) => {
    let bytes = 0;
    let gz = 0;
    let n = 0;
    for (const [path, text] of out) {
      if (!filter(path)) continue;
      n++;
      bytes += Buffer.byteLength(text);
      gz += gzipSync(text, { level: 9 }).length;
    }
    return `${n} files, ${(bytes / 1e6).toFixed(2)} MB (gzip ${(gz / 1e6).toFixed(2)} MB)`;
  };
  const summary = [
    `driving-simulator ${version}: ${langs.length} languages of ${keys} texts, ${examLangs.length} with the words of the learning modes; stamp ${stamp}`,
    `  web/lang/<code>.json       ${size((p) => /^web\/lang\/[a-z]+\.json$/.test(p) && !p.endsWith('index.json'))}`,
    `  web/lang/exam/<code>.json  ${size((p) => p.startsWith('web/lang/exam/'))}`,
    `  web/lang (all)             ${size((p) => p.startsWith('web/lang/'))}`,
    `  src/i18n (built in)        ${size((p) => p.startsWith('src/i18n/'))}`,
  ];
  return { stale, summary };
}

// ------------------------------------------------------------------ the classes of characters

const hex = (n) => `0x${n.toString(16)}`;
/** Lines of at most about 150 characters. */
function wrap(items, indent = '    ') {
  const lines = [];
  let line = '';
  for (const item of items) {
    if (line !== '' && line.length + item.length + 2 > 150) {
      lines.push(`${indent}${line},`);
      line = '';
    }
    line = line === '' ? item : `${line}, ${item}`;
  }
  if (line !== '') lines.push(`${indent}${line},`);
  return lines.join('\n');
}

/**
 * src/i18n/unicode.inc: what unicode.cpp answers isLetter(), isLatin(), toUpperCase() … from.
 * Every code point is put to this node's own regular expressions and toUpperCase(), so the
 * tables say what the original's expressions say (as of the Unicode version of the node).
 */
function exportUnicode() {
  const classes = [
    ['LETTER', /\p{L}/u, '\\p{L}'],
    ['UPPER_LETTER', /\p{Lu}/u, '\\p{Lu}'],
    ['LOWER_LETTER', /\p{Ll}/u, '\\p{Ll}'],
    ['NUMBER', /\p{N}/u, '\\p{N}'],
    ['LATIN', /\p{Script=Latin}/u, '\\p{Script=Latin}'],
    ['CYRILLIC', /\p{Script=Cyrillic}/u, '\\p{Script=Cyrillic}'],
    ['ARABIC', /\p{Script=Arabic}/u, '\\p{Script=Arabic}'],
    ['PICTOGRAPHIC', /\p{Extended_Pictographic}/u, '\\p{Extended_Pictographic}'],
  ];
  const text = [
    `// GENERATED by tools/i18n-export.mjs --unicode with node ${process.version} (Unicode ${process.versions.unicode}) – do not edit (run the script again).`,
    '// The classes of characters the regular expressions of the original ask for: runs of code points, first … last.',
  ];
  for (const [name, re, what] of classes) {
    const runs = [];
    let start = -1;
    for (let cp = 0; cp <= 0x110000; cp++) {
      const inside = cp <= 0x10ffff && re.test(String.fromCodePoint(cp));
      if (inside && start < 0) start = cp;
      if (!inside && start >= 0) {
        runs.push(`{${hex(start)}, ${hex(cp - 1)}}`);
        start = -1;
      }
    }
    text.push(`// ${what}`, `static const Run ${name}[] = {`, wrap(runs), '};');
  }
  // toUpperCase(): one character each (most of them: a run of neighbours, or of every second one, moved by the same amount) or several
  const single = [];
  const several = [];
  for (let cp = 0; cp <= 0x10ffff; cp++) {
    if (cp >= 0xd800 && cp <= 0xdfff) continue;
    const s = String.fromCodePoint(cp);
    const up = [...s.toUpperCase()].map((ch) => ch.codePointAt(0));
    if (up.length === 1 && up[0] === cp) continue;
    if (up.length === 1) single.push([cp, up[0] - cp]);
    else if (up.length <= 3) several.push(`{${hex(cp)}, {${[...up, 0, 0].slice(0, 3).map(hex).join(', ')}}}`);
    else fail(`toUpperCase of U+${cp.toString(16)}: more than three characters`);
  }
  const runs = [];
  for (let i = 0; i < single.length; ) {
    const [first, delta] = single[i];
    let step = 1;
    let j = i + 1;
    if (j < single.length && single[j][1] === delta && (single[j][0] === first + 1 || single[j][0] === first + 2)) {
      step = single[j][0] - first;
      while (j < single.length && single[j][1] === delta && single[j][0] === single[j - 1][0] + step) j++;
    }
    runs.push(`{${hex(first)}, ${hex(single[j - 1][0])}, ${step}, ${delta}}`);
    i = j;
  }
  text.push(
    '// toUpperCase(): from first to last, every `step`th code point becomes itself plus `delta` …',
    'static const CaseRun TO_UPPER[] = {',
    wrap(runs),
    '};',
    '// … and these become two or three characters',
    'static const CaseSeveral TO_UPPER_SEVERAL[] = {',
    wrap(several),
    '};',
  );
  out.set('src/i18n/unicode.inc', `${text.join('\n')}\n`);
}

// ------------------------------------------------------------------ the native host's Intl

/** The languages the native host's Intl knows: those of the game, and American English ("en", the original's fallback). */
const INTL_TAGS = ['de-DE', 'en-GB', 'en', 'id-ID', 'cs-CZ', 'es-ES', 'fr-FR', 'hr-HR', 'it-IT', 'hu-HU', 'nl-NL', 'pl-PL', 'pt-PT', 'ro-RO', 'sq-AL', 'vi-VN', 'tr-TR', 'el-GR', 'bg-BG', 'ru-RU', 'uk-UA', 'ar', 'fa-IR', 'ur-PK', 'hi-IN', 'bn-BD', 'zh-CN', 'ja-JP', 'ko-KR'];
const INTL_UNITS = ['meter', 'kilometer', 'minute', 'kilometer-per-hour'];
const INTL_BEGIN = '// >>> GENERATED by tools/i18n-export.mjs --intl';
const INTL_END = '// <<< GENERATED';

/** A C++ string literal: letters and signs as they are, what cannot be seen (spaces of all kinds, marks of direction) as bytes. */
function cVisible(s) {
  let lit = '';
  for (const ch of s) {
    const cp = ch.codePointAt(0);
    if (ch === '"' || ch === '\\') lit += `\\${ch}`;
    else if (cp >= 0x20 && cp < 0x7f) lit += ch;
    else if (/[\p{L}\p{M}\p{N}\p{P}\p{S}]/u.test(ch)) lit += ch;
    else for (const b of Buffer.from(ch, 'utf8')) lit += `\\${b.toString(8).padStart(3, '0')}`;
  }
  return `"${lit}"`;
}

/**
 * The table between the two marks in src/platform/native/intl_native.cpp: for each language the
 * signs of its numbers, the words between the items of a list, and how it writes a length, a
 * distance, a time and a speed, short and in words, for each of its plural forms – all asked
 * of this node's own Intl (CLDR), which is what a browser answers too.
 */
function exportIntl() {
  const path = 'src/platform/native/intl_native.cpp';
  const abs = join(ROOT, path);
  if (!existsSync(abs)) fail(`${path} is not there`);
  const source = readFileSync(abs, 'utf8');
  const begin = source.indexOf(INTL_BEGIN);
  const end = source.indexOf(INTL_END);
  if (begin < 0 || end < begin) fail(`${path}: the marks "${INTL_BEGIN}" … "${INTL_END}" are not there`);

  const number = new Set(['integer', 'group', 'decimal', 'fraction', 'minusSign', 'plusSign', 'nan', 'infinity']);
  const locales = [];
  const units = [];
  for (const tag of INTL_TAGS) {
    // the signs of a number
    const symbols = (options) => {
      const nf = new Intl.NumberFormat(tag, { minimumFractionDigits: 1, maximumFractionDigits: 1, ...options });
      const get = (v, type) => nf.formatToParts(v).find((p) => p.type === type)?.value;
      const whole = (v) => nf.formatToParts(v).filter((p) => p.type === 'integer').map((p) => p.value.length);
      const sizes = whole(1234567.5);
      if (sizes.length !== 3 || sizes[2] !== 3 || sizes[0] + sizes[1] !== 4) fail(`${tag}: groups of digits of sizes ${sizes}`);
      // (what stands before a negative number: the minus, in the languages written from right to left with a mark of direction before it)
      const negative = nf.formatToParts(-1.5);
      const digitsAt = negative.findIndex((p) => p.type === 'integer');
      if (digitsAt < 1 || negative[negative.length - 1].type !== 'fraction') fail(`${tag}: a negative number that is not a sign before the digits`);
      const minus = negative.slice(0, digitsAt).map((p) => p.value).join('');
      return { decimal: get(1.5, 'decimal'), group: get(1234567.5, 'group'), minGroup: whole(1234.5).length > 1 ? 1 : whole(12345.5).length > 1 ? 2 : fail(`${tag}: no groups of digits`), group2: sizes[1], minus, zero: get(0, 'integer'), nan: get(NaN, 'nan'), infinity: get(Infinity, 'infinity') };
    };
    const latn = symbols({ numberingSystem: 'latn' });
    const own = symbols({});
    const ownSystem = new Intl.NumberFormat(tag).resolvedOptions().numberingSystem;
    if (latn.zero !== '0') fail(`${tag}: the digits asked for as Western are not`);
    if (own.minGroup !== latn.minGroup || own.group2 !== latn.group2 || own.minus !== latn.minus || own.nan !== latn.nan || own.infinity !== latn.infinity) fail(`${tag}: its own digits are grouped or signed in another way`);
    // the words of a list
    const list = (items) => {
      const parts = new Intl.ListFormat(tag, { style: 'long', type: 'conjunction' }).formatToParts(items);
      if (parts.length !== items.length * 2 - 1 || parts.some((p, i) => (i % 2 === 0 ? p.type !== 'element' || p.value !== items[i / 2] : p.type !== 'literal'))) fail(`${tag}: a list that is not its items with words between them`);
      return parts.filter((p) => p.type === 'literal').map((p) => p.value);
    };
    const [pair] = list(['A', 'B']);
    const [start3, end3] = list(['A', 'B', 'C']);
    const five = list(['A', 'B', 'C', 'D', 'E']);
    if (five[0] !== start3 || five[3] !== end3 || five[1] !== five[2]) fail(`${tag}: the words of a list change with its length`);
    locales.push(`    {${[cVisible(tag), cVisible(latn.decimal), cVisible(latn.group), latn.minGroup, latn.group2, cVisible(latn.minus), cVisible(latn.nan), cVisible(latn.infinity), ownSystem === 'latn' ? 0 : hex(own.zero.codePointAt(0)), cVisible(ownSystem === 'latn' ? '' : own.decimal), cVisible(ownSystem === 'latn' ? '' : own.group), cVisible(pair), cVisible(start3), cVisible(five[1]), cVisible(end3)].join(', ')}},`);

    // the units, for each plural form of the language
    const samples = [];
    for (const v of [0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 19, 20, 21, 22, 23, 24, 25, 100, 101, 102, 103, 104, 105, 111, 112, 113, 121, 122, 1000, 1000000, 2000000, 5000000]) samples.push([v, 0]);
    for (const v of [0, 0.1, 0.2, 0.5, 1, 1.1, 1.2, 1.5, 2, 2.1, 2.5, 5, 10, 11.1, 21.1, 22.2]) samples.push([v, 1]);
    for (const v of [0.01, 0.02, 0.11, 0.21, 1, 1.21, 1.5, 2.22, 5.05]) samples.push([v, 2]);
    for (const unit of INTL_UNITS) {
      for (const display of ['short', 'long']) {
        const patterns = new Map();
        for (const [v, digits] of samples) {
          const digitOptions = { minimumFractionDigits: digits, maximumFractionDigits: digits };
          const category = new Intl.PluralRules(tag, digitOptions).select(v);
          let pattern = '';
          let placed = false;
          for (const p of new Intl.NumberFormat(tag, { style: 'unit', unit, unitDisplay: display, numberingSystem: 'latn', useGrouping: false, ...digitOptions }).formatToParts(v)) {
            if (!number.has(p.type)) pattern += p.value;
            else if (!placed) {
              pattern += '{0}';
              placed = true;
            }
          }
          if (pattern.replace('{0}', '').includes('{0}')) fail(`${tag} ${unit}: the number twice`);
          if (patterns.has(category) && patterns.get(category) !== pattern) fail(`${tag} ${unit} ${display}: "${patterns.get(category)}" and "${pattern}" for the same plural form ${category} (at ${v})`);
          patterns.set(category, pattern);
        }
        if (!patterns.has('other')) fail(`${tag} ${unit}: no form "other"`);
        for (const category of ['zero', 'one', 'two', 'few', 'many', 'other']) {
          if (!patterns.has(category)) continue;
          // (a form written like "other" needs no line of its own)
          if (category !== 'other' && patterns.get(category) === patterns.get('other')) continue;
          units.push(`    {${[cVisible(tag), cVisible(unit), `'${display[0]}'`, cVisible(category), cVisible(patterns.get(category))].join(', ')}},`);
        }
      }
    }
  }
  const table = [
    `${INTL_BEGIN} with node ${process.version} (CLDR ${process.versions.cldr}) – do not edit between the marks (run the script again).`,
    '// tag; decimal sign, sign between the groups of digits, how many digits the first group needs before a number is grouped,',
    '// size of the groups after the first, minus, what is no number, what is without end; the first of its own digits where they',
    '// are not the Western ones (0: they are), with their decimal and group signs; the words of a list: between two, after the',
    '// first, in the middle, before the last',
    'static const LocaleRow LOCALES[] = {',
    ...locales,
    '};',
    "// tag, unit, 's'hort or 'l'ong, plural form, the text ({0}: the number) – a form that is not listed is written like \"other\"",
    'static const UnitRow UNITS[] = {',
    ...units,
    '};',
  ].join('\n');
  out.set(path, `${source.slice(0, begin)}${table}\n${source.slice(end)}`);
}

// ------------------------------------------------------------------ writing

function finish(stale, summary) {
  let changed = 0;
  for (const [path, text] of out) {
    const abs = join(ROOT, path);
    const old = existsSync(abs) ? readFileSync(abs, 'utf8') : null;
    if (old === text) continue;
    changed++;
    console.log(`${CHECK ? 'would write' : 'wrote'} ${path}${old === null ? ' (new)' : ''}`);
    if (CHECK) continue;
    mkdirSync(dirname(abs), { recursive: true });
    writeFileSync(abs, text);
  }
  for (const path of stale) {
    changed++;
    console.log(`${CHECK ? 'would remove' : 'removed'} ${path}`);
    if (!CHECK) rmSync(join(ROOT, path));
  }
  for (const line of summary) console.log(line);
  console.log(changed === 0 ? 'nothing has changed' : `${changed} file(s) ${CHECK ? 'differ' : 'written or removed'} under ${relative(process.cwd(), ROOT) || '.'}`);
  process.exit(CHECK && changed > 0 ? 1 : 0);
}

if (flags.has('--unicode') || flags.has('--intl')) {
  if (flags.has('--unicode')) exportUnicode();
  if (flags.has('--intl')) exportIntl();
  finish([], []);
} else {
  const { stale, summary } = await exportTexts(resolve(paths[0] ?? '/home/user/driving-simulator'));
  finish(stale, summary);
}
