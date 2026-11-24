// SPDX-License-Identifier: AGPL-3.0-only
/**
 * What the program knows before any language is fetched – the tables of builtin.cpp, which
 * tools/i18n-export.mjs writes from the original's src/i18n (see there):
 *
 *   - the table LANGS of index.ts and the languages of EXAM_LANGS (exam/index.ts);
 *   - the stamp of the texts, which goes into the address of every language file that is
 *     fetched ("lang/fr.json?v=<stamp>"): a browser that still has the files of an earlier
 *     build never mixes them with this one's;
 *   - the texts that are part of the program: German and English (the original imports them
 *     at once – the loading screen and the messages of a failed start need words before any
 *     file has arrived) and the English words of the learning modes (EXAM_EN), each as the
 *     JSON of its file in web/lang.
 *
 * All of it is constant data (no constructors run for it), so it may be asked for at any
 * time, also while the program starts.  For src/i18n itself: the game asks index.h.
 * (Nothing but <cstddef> is included here: builtin.cpp, with its half megabyte of texts, is
 * compiled in a moment.)
 */
#pragma once

#include <cstddef>

namespace fs::i18n {

struct BuiltinLang {
  const char* code;
  const char* name;
  const char* locale;
  bool rtl;
  /** part of the program (no loader in LANGS) */
  bool builtin;
};

struct BuiltinText {
  const char* code;
  const char* json;
  size_t size;
};

extern const char BUILTIN_STAMP[];
/** the version of the original the texts were taken from ("1.7.7") */
extern const char BUILTIN_SOURCE[];
extern const BuiltinLang BUILTIN_LANGS[];
extern const size_t BUILTIN_LANG_COUNT;
extern const char* const BUILTIN_EXAM_LANGS[];
extern const size_t BUILTIN_EXAM_LANG_COUNT;
extern const BuiltinText BUILTIN_DICTS[];
extern const size_t BUILTIN_DICT_COUNT;
extern const BuiltinText BUILTIN_EXAM_EN;

}  // namespace fs::i18n
