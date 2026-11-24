// SPDX-License-Identifier: AGPL-3.0-only
/**
 * Characters as JavaScript's strings and regular expressions see them.
 *
 * The original works on the texts with regular expressions (\s, \w, \p{L},
 * \p{Script=Cyrillic} …), with lengths and positions counted in UTF-16 units, and with
 * toUpperCase().  The port's texts are UTF-8; these are the pieces that make the
 * hand-written matchers of src/i18n read a text exactly as the original's expressions do.
 * The classes of characters come from Unicode's own tables (unicode.inc, written by
 * `node tools/i18n-export.mjs --unicode`).
 */
#pragma once

#include <string>
#include <string_view>

namespace fs::i18n {

/** The characters (code points) of a UTF-8 text; a byte that is no UTF-8 becomes U+FFFD, as in a browser. */
std::u32string decodeUtf8(std::string_view s);
std::string encodeUtf8(std::u32string_view s);
void appendUtf8(std::string& out, char32_t c);

/** The text as JavaScript holds it, and back (half a surrogate pair becomes U+FFFD). */
std::u16string toUtf16(std::string_view s);
std::string fromUtf16(std::u16string_view s);
/** `s.length` */
size_t utf16Length(std::string_view s);
/** `[...s].length` */
size_t codePointLength(std::string_view s);

/** \s (and what trim() takes away): white space and line ends of any script */
bool isSpace(char32_t c);
/** \d */
inline bool isDigit(char32_t c) { return c >= U'0' && c <= U'9'; }
/** \w */
inline bool isWord(char32_t c) { return (c >= U'0' && c <= U'9') || (c >= U'A' && c <= U'Z') || (c >= U'a' && c <= U'z') || c == U'_'; }

/** \p{L} */
bool isLetter(char32_t c);
/** \p{Lu} */
bool isUpperLetter(char32_t c);
/** \p{Ll} */
bool isLowerLetter(char32_t c);
/** \p{N} */
bool isNumber(char32_t c);
/** \p{Script=Latin}, \p{Script=Cyrillic}, \p{Script=Arabic} */
bool isLatin(char32_t c);
bool isCyrillic(char32_t c);
bool isArabic(char32_t c);
/** \p{Extended_Pictographic} */
bool isPictographic(char32_t c);

/** `s.toUpperCase()` ("ß" becomes "SS") */
std::string toUpperCase(std::string_view s);
/** `s.trim()` */
std::string trimJs(std::string_view s);

/** Does `word` stand in `s` at position `at`? */
inline bool startsAt(std::u32string_view s, size_t at, std::u32string_view word) { return at <= s.size() && s.substr(at, word.size()) == word; }

}  // namespace fs::i18n
