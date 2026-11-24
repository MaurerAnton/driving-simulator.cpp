#!/usr/bin/env node
// SPDX-License-Identifier: AGPL-3.0-only
/**
 * The texts of the original, made into the data the port ships and builds in.
 *
 *   node tools/i18n-export.mjs [original]            write the texts
 *   node tools/i18n-export.mjs --check [original]    write nothing: say what would change (exit 1 if anything would)
 *   node tools/i18n-export.mjs --unicode             src/i18n/unicode.inc from this node's Unicode data
 *   node tools/i18n-export.mjs --intl                the CLDR table in src/platform/native/intl_native.cpp from this node's ICU
 *   node tools/i18n-export.mjs --answers dir [original]   what the original's src/i18n answers, for the port to be compared with
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
 *
 * --answers is the comparison with the original (docs/PORT.md: the strongest check there is).  It
 * runs the original's own code – tr() over every text of every language with keys and with touch
 * buttons, speakable() and splitForSpeech() over them and over texts made to be hard, the rules,
 * citations, key names, quantities, the words of the learning modes, and its checker over real
 * and spoilt translations – and writes the answers to dir/original (and what it asked to dir/in).
 * The port writes its own to dir/port, and the two must not differ:
 *
 *   node tools/i18n-export.mjs --answers /tmp/i18n && tools/t.sh i18n_test --answers /tmp/i18n && diff -r /tmp/i18n/original /tmp/i18n/port
 *
 * (The quantities in words come from this node's CLDR on the one side and from the native host's
 * table on the other: after a change of node, --intl first.)
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
  if (!['--check', '--unicode', '--intl', '--answers'].includes(f)) {
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
    const original = (await load(`${l.code}.ts`)).default;
    if (original === null || typeof original !== 'object' || Array.isArray(original)) fail(`${l.code}.ts: does not export an object of texts`);
    // the texts new in 2.0 (i18n-extra/<code>.json, every language): after the original's, which they never replace
    const extraFile = join(ROOT, 'i18n-extra', `${l.code}.json`);
    const extra = existsSync(extraFile) ? JSON.parse(readFileSync(extraFile, 'utf8')) : {};
    for (const k of Object.keys(extra)) if (k in original) fail(`i18n-extra/${l.code}.json: ${k} is a text of the original already`);
    const dict = { ...original, ...extra };
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

/**
 * The languages the native host's Intl knows: those of the game (the locales of web/lang/index.json, which
 * the texts' export has written), and American English ("en": the original falls back to it by that name).
 */
function intlTags() {
  const index = join(ROOT, 'web/lang/index.json');
  if (!existsSync(index)) fail('web/lang/index.json is not there: export the texts first');
  const tags = JSON.parse(readFileSync(index, 'utf8')).langs.map((l) => l.locale);
  if (!tags.includes('en')) tags.splice(tags.indexOf('en-GB') + 1, 0, 'en');
  return tags;
}
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

  // (the plural rules are written out by hand there: a language without one would be written as English)
  const rules = source.slice(source.indexOf('const char* pluralOf('));
  const ruled = new Set([...rules.slice(0, rules.indexOf('\n}\n')).matchAll(/lang == "([a-z]+)"/g)].map((m) => m[1]));
  const tags = intlTags();
  for (const tag of tags) if (!ruled.has(tag.split('-')[0])) fail(`${tag}: pluralOf() in ${path} has no rule for this language – write CLDR's rule there first`);

  const number = new Set(['integer', 'group', 'decimal', 'fraction', 'minusSign', 'plusSign', 'nan', 'infinity']);
  const locales = [];
  const units = [];
  for (const tag of tags) {
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

// ------------------------------------------------------------------ the original's answers

/**
 * What the original answers (see the head of this file), as lines of text: dir/original/*.txt.
 * What the port cannot know by itself – the texts made to be hard, the spoilt translation, the
 * texts with a citation – goes to dir/in, where tests/i18n_test.cpp reads it.
 */
async function exportAnswers(dir, original) {
  const load = async (file) => import(pathToFileURL(join(original, file)).href);
  const { LANGS, loadLang, tr, setKeyStyle, keyName, violationRef, violationTitle, violationWhy, formatUnit, formatMonth, pickLang, dictOf, lessonTitle, lessonIntro, isRtl, localeOf, langInfo } = await load('src/i18n/index.ts');
  const { speakable, splitForSpeech } = await load('src/i18n/speechtext.ts');
  const { VIOLATION_REFS, citationParts } = await load('src/i18n/violations.ts');
  const { loadExamTexts, EXAM_LANGS } = await load('src/i18n/exam/index.ts');
  const { checkDict } = await load('tools/i18n-check.ts');
  const { touchLabel } = await load('src/i18n/keys.ts');

  const esc = (s) => String(s).replace(/\\/g, '\\\\').replace(/\n/g, '\\n').replace(/\r/g, '\\r').replace(/\t/g, '\\t');
  const US = String.fromCharCode(0x1f);
  const cp = (...points) => String.fromCodePoint(...points);
  const files = new Map();
  for (const l of LANGS) if (!(await loadLang(l.code))) fail(`${l.code} cannot be loaded`);
  const codes = LANGS.map((l) => l.code);
  const en = dictOf('en');
  const keys = Object.keys(en);

  /** the values a text is filled with: a number, a word, a text with signs in it – by the place of each placeholder in the English text */
  const paramsOf = (key) => {
    const p = {};
    let i = 0;
    for (const m of (en[key] ?? '').matchAll(/\{(\w+)\}/g)) {
      if (m[1] in p) continue;
      p[m[1]] = i % 3 === 0 ? 12.5 : i % 3 === 1 ? `<${m[1]}>` : 'a $& b {d} c';
      i++;
    }
    return p;
  };

  // every text of every language (and of one that is not there), with keys and with the buttons of a touch screen
  {
    const out = [];
    for (const l of [...codes, 'xx']) {
      for (const k of [...keys, 'no.such.key']) {
        const p = paramsOf(k);
        setKeyStyle('keys');
        const a = tr(k, l, p);
        setKeyStyle('touch');
        const b = tr(k, l, p);
        out.push(`${l}\t${k}\t${esc(a)}\t${a === b ? '=' : esc(b)}`);
      }
    }
    setKeyStyle('keys');
    files.set('original/tr.txt', out);
  }

  // the same texts ready to be read out and cut into pieces – and texts made to be hard
  const hard = [
    'In 200 m turn right', '12,345 m ahead', '1.5km/h and 3 m.', '5 mm, 7 m2, 10 m. 9 m/s', '200m', '200 m先', '3 км/год, 4 км/ч, 5 км, 6 м', '7 م و 8 كم و 9 كم/س', '1,25 km; 1,256 km; 0.5 m', '30 km/h.', '120km/hx', '5 m5', '5 mé', '1 000 m', '2 m²', '99 m!',
    'die 1. Ausfahrt, die 2.  Ausfahrt, die 10. Ausfahrt, die 11. Ausfahrt, die 0. Ausfahrt, die 123. Ausfahrt, 3.Ausfahrt', 'la 3.ª salida, la 3ª salida, la 13.ª salida, 2.ª saída', 'de 2e afslag, 12e afslag', '1. výjezdem 2. zjazdem 3. izlazu 4. çıkış 5. kijárat',
    "2η έξοδο, 3-й съезд, 4-й з'їзд, 5-й з’їзд", 'jalan keluar ke-2, ke-10, ke-11, ke-123, make-1, ke-0',
    'z. B. hier, z.B. dort, bzw. so, ggf. anders, xbzw. nicht, StVO, StVOx, § 5 StVO, e.g. this, i.e. that, nr. 5, nr.6, nr. x', 'Trả lời 3/5, 10 / 20, 1/2/3, a/b',
    'Press [[KeyG]] ★★☆ · then ↑', '[[KeyB]] [[Space]] [[Enter]] [[Escape]] [[ShiftLeft]] [[ArrowUp]] [[Digit3]] [[F5]] [[Period]] [[KeyZ]] [[KeyQ]] [[KeyW]] [[KeyM]] [[KeyY]]', '[[', '[[Key G]]', '[[[KeyB]]]', '[[]]',
    'a | b · c|d', 'a – b — c–d - e', '„x“ "y" «z» ‹w›', '(a) ( b ) c(d)e', 'a ,, b , , c ,. d , ! e', ' , lead and trail , ', 'x  \t y\n z', `${cp(0x1f697)} car ✓ ok ✗ no • dot ▲▼◀▶ ⋯`, 'a (b), c', 'end (x).', ',,,', '', '   ',
    `${cp(0x2066)}§ 37 StVO${cp(0x2069)}`, `soft${cp(0xad)}hyphen zero${cp(0x200b)}width ${cp(0x200e)}ltr${cp(0x200f)}`, `می${cp(0x200c)}رسند`,
    'Eins zwei drei. '.repeat(30), Array.from({ length: 20 }, (_, i) => `part ${i}`).join(', '), 'word '.repeat(60), '城区内至少1.5米。城区外至少2米。'.repeat(10), 'At least 1.5 metres. Go.', 'One! Two? Three… Four.Five. Six', 'क्या यह ठीक है। हाँ! नहीं?', 'هل هذا صحيح؟ نعم. لا۔ ربما', 'A, b; c: d، e、f，g；h：i',
    'x'.repeat(400), 'ab cd '.repeat(80), 'a。b。。c。', 'First sentence is here.   Second one follows!Third?   ', 'no stop at all but, commas, here, and, there, everywhere, in, this, long, long, long, line, of, text, that, goes, on',
  ];
  files.set('in/hard.txt', hard.map(esc));
  {
    const out = [];
    const sizes = [180, 60, 25, 7, 1];
    // (a character beyond U+FFFF is cut in two by the original where the port keeps it whole: such a text is not cut here)
    const beyond = /[\u{10000}-\u{10ffff}]/u;
    const row = (l, k, text) => out.push(`${l}\t${k}\t${esc(speakable(text, l))}\t${beyond.test(text) ? '-' : sizes.map((m) => esc(splitForSpeech(text, m).join(US))).join('\t')}`);
    for (const l of codes) {
      for (const k of keys) row(l, k, tr(k, l, paramsOf(k)));
      hard.forEach((text, i) => row(l, `hard${i}`, text));
    }
    files.set('original/speak.txt', out);
  }

  // rules, lessons, quantities, citations, the choice of a language, the names of the keys
  {
    const out = [];
    for (const l of [...codes, 'xx']) {
      for (const id of [...Object.keys(VIOLATION_REFS), 'nothing']) out.push(`ref\t${l}\t${id}\t${esc(violationRef(id, l))}\t${esc(violationTitle(id, l, { speed: 62, limit: 50, n: 3 }))}\t${esc(violationWhy(id, l))}`);
      for (const id of ['basics', 'rbl', 'motorway', 'ceExam', 'nope']) out.push(`lesson\t${l}\t${id}\t${esc(lessonTitle(id, l))}\t${esc(lessonIntro(id, l))}`);
      out.push(`lang\t${l}\t${isRtl(l)}\t${localeOf(l)}\t${langInfo(l).name}`);
      for (const [v, d] of [[5.12, 1], [1234.5, 1], [7, 0], [35, 0], [0.4, 0], [1, 0], [2, 2], [21, 0], [1e6, 0], [-3.5, 1]]) for (const u of ['kilometer', 'meter', 'minute']) out.push(`unit\t${l}\t${v}\t${d}\t${u}\t${esc(formatUnit(v, u, l, d))}`);
      // (the native host has the dates of German and English)
      if (l === 'de' || l === 'en' || l === 'xx') for (const t of [Date.UTC(2023, 4, 17), 0, Date.UTC(2026, 11, 31, 23, 59), Date.UTC(2024, 1, 29)]) out.push(`month\t${l}\t${t}\t${esc(formatMonth(t, l))}`);
    }
    const cited = new Set(Object.values(VIOLATION_REFS));
    for (const l of ['de', 'en', 'ar']) for (const v of Object.values(dictOf(l))) if (v.includes('§') || v.includes('Anlage')) cited.add(v);
    for (const t of ['', 'x', '§ 1', '§ 1 Abs. 2', '§ 34 Abs. 1 StVO, § 323c StGB', '§ 54 Abs. 2 BOStrab', 'Anlage 7 FeV, Nr. 2.1.4.5', 'Anlage 2', 'Anlage 2 StVOx', '§ 9 Abs. 1, Abs. 3, Abs. 5 StVO', '§ 9 Abs. , Abs. 3', '§ 3a Abs. 2b, Abs. x', '§§ 1, 2 StVO', '§ 12StVO', 'xStVO § 3 FeV', '§ 5 (1) UrhG']) cited.add(t);
    files.set('in/cited.txt', [...cited].map(esc));
    for (const t of cited) out.push(`cite\t${esc(t)}\t${citationParts(t).map((p) => `[${esc(p.text)}|${p.url ?? '-'}]`).join('')}`);
    const preferred = [['de-AT', 'en'], ['en-US'], ['xx', 'yy'], ['sr-RS', 'en'], ['gsw-CH'], [], ['PT-br'], ['zh-Hans-CN', 'en'], ['nds'], ['sk', 'cs'], ['ms-MY'], ['', 'fr'], ['-de'], ['be-BY'], ['EN'], ['lb-LU', 'fr-LU'], ['mo'], ['ca-ES', 'es'], ['ja-JP-u-ca-japanese']];
    files.set('in/preferred.txt', preferred.map((p) => p.join(',')));
    for (const p of preferred) out.push(`pick\t${p.join(',')}\t${pickLang(p)}`);
    const keyCodes = ['Space', 'Enter', 'NumpadEnter', 'ShiftLeft', 'ShiftRight', 'Escape', 'ArrowUp', 'ArrowDown', 'ArrowLeft', 'ArrowRight', 'Digit0', 'Digit1', 'Digit9', 'Period', 'F1', 'F12', 'Tab', 'Comma', 'Key', 'Digit', '', ...'ABCDEFGHIJKLMNOPQRSTUVWXYZ'.split('').map((c) => `Key${c}`)];
    for (const l of [...codes, 'xx']) out.push(`keys\t${l}\t${keyCodes.map((c) => esc(keyName(c, l))).join(' ')}\t${keyCodes.map((c) => esc(touchLabel(c, l, (k) => tr(k, l)))).join(US)}`);
    files.set('original/misc.txt', out);
  }

  // the words of the learning modes, in every language that has them and in one that has not
  {
    const out = [];
    for (const l of [...Object.keys(EXAM_LANGS), 'xx']) {
      const t = await loadExamTexts(l);
      for (const part of ['questions', 'danger']) for (const [id, w] of Object.entries(t[part])) out.push(`${l}\t${part}\t${id}\t${esc(w.q)}\t${w.a.map(esc).join(US)}\t${esc(w.why)}`);
      for (const [id, w] of Object.entries(t.signs)) out.push(`${l}\tsigns\t${id}\t${esc(w)}`);
    }
    files.set('original/exam.txt', out);
  }

  // the checker: every language, languages taken for one another, and a translation spoilt in many ways
  {
    const out = [];
    const say = (name, r) => {
      out.push(`# ${name}: ${r.errors.length} errors, ${r.warnings.length} warnings`);
      for (const e of r.errors) out.push(`E ${esc(e)}`);
      for (const w of r.warnings) out.push(`W ${esc(w)}`);
    };
    for (const l of codes) say(l, checkDict(l, dictOf(l)));
    for (const [code, other] of [['ru', 'en'], ['de', 'fr'], ['zh', 'ja'], ['ja', 'de'], ['xx', 'de'], ['en', 'de'], ['el', 'ru'], ['ar', 'fa'], ['ko', 'zh'], ['hi', 'bn'], ['fr', 'en'], ['uk', 'ru'], ['bn', 'hi'], ['fa', 'ur'], ['tr', 'vi'], ['bg', 'el']]) say(`${code} with the texts of ${other}`, checkDict(code, dictOf(other)));
    say('fr against de', checkDict('fr', dictOf('fr'), dictOf('de')));
    const spoilt = { ...en };
    keys.forEach((k, i) => {
      const v = en[k];
      const how = i % 23;
      if (how === 0) delete spoilt[k];
      else if (how === 1) spoilt[k] = ` ${v}`;
      else if (how === 2) spoilt[k] = v.replace(/\{(\w+)\}/, '{x$1}');
      else if (how === 3) spoilt[k] = v.replace(/\[\[(\w+)\]\]/, '$1');
      else if (how === 4) spoilt[k] = v.replace(/\d/g, (d) => String.fromCharCode(0x660 + Number(d)));
      else if (how === 5) spoilt[k] = `${v} <b>`;
      else if (how === 6) spoilt[k] = `${v}\nline`;
      else if (how === 7) spoilt[k] = v.replace(/[A-Z]{2,}/, 'xx');
      else if (how === 8) spoilt[k] = `${v}${v}${v}`;
      else if (how === 9) spoilt[k] = `${v}  x`;
      else if (how === 10) spoilt[k] = `${v}{`;
      else if (how === 11) spoilt[k] = v.replace(/\d+/, '');
      else if (how === 12) spoilt[k] = '';
      else if (how === 13) spoilt[k] = `${v}.`;
      else if (how === 14) spoilt[k] = v.replace(/[.!?…]$/, '');
      else if (how === 15) spoilt[k] = v.charAt(0).toUpperCase() + v.slice(1);
      else if (how === 16) spoilt[k] = `${v}${String.fromCharCode(0x200f)}`;
      else if (how === 17) spoilt[k] = v.replace(/\./g, ',');
      else if (how === 18) spoilt[k] = `[[${v}]]`;
      else if (how === 19) spoilt[k] = v.replace(/§/g, 'Par.');
    });
    spoilt.extra1 = 'x';
    spoilt['extra.2'] = 'y';
    files.set('in/spoilt.json', [JSON.stringify(spoilt)]);
    for (const code of ['xx', 'de', 'zh', 'ru', 'en']) say(`${code} with spoilt texts`, checkDict(code, spoilt));
    files.set('original/check.txt', out);
  }

  let lines = 0;
  for (const [name, list] of files) {
    const path = join(dir, name);
    mkdirSync(dirname(path), { recursive: true });
    writeFileSync(path, `${list.join('\n')}\n`);
    if (name.startsWith('original/')) lines += list.length;
  }
  console.log(`${lines} answers of the original in ${join(dir, 'original')}; now: tools/t.sh i18n_test --answers ${dir} && diff -r ${join(dir, 'original')} ${join(dir, 'port')}`);
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

if (flags.has('--answers')) {
  if (paths.length < 1) fail('--answers: the folder to write to is missing');
  await exportAnswers(resolve(paths[0]), resolve(paths[1] ?? '/home/user/driving-simulator'));
} else if (flags.has('--unicode') || flags.has('--intl')) {
  if (flags.has('--unicode')) exportUnicode();
  if (flags.has('--intl')) exportIntl();
  finish([], []);
} else {
  const { stale, summary } = await exportTexts(resolve(paths[0] ?? '/home/user/driving-simulator'));
  finish(stale, summary);
}
