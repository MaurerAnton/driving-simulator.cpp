// SPDX-License-Identifier: AGPL-3.0-only
// Origin: src/i18n/speechtext.ts @ v1.7.7
/**
 * Texts made ready to be read out.
 *
 * The texts are written to be read on the screen: "In 200 m turn right",
 * "die 2. Ausfahrt", "la 2.ª salida", "[[KeyG]]", a dash, a star.  A voice of
 * the browser reads many of these badly – a lone "m" as the letter, "2.ª" as
 * "two point a", a Latin "m" inside Greek or Hindi text as the English
 * letter, an arrow as "leftwards arrow".  speakable() turns such pieces into
 * the words they stand for, language by language: a number with its unit
 * becomes the long form the language writes (the plural and case right, from
 * the browser's own Intl data: "200 metrów", "1,5 метра"), the ordinal of a
 * roundabout exit the word in the case the sentence wants, symbols a pause or
 * nothing.  splitForSpeech() then cuts a long text into pieces a voice reads
 * in one go (Chrome stops a voice after about 15 seconds).
 *
 * Pure functions, no DOM: tests/i18n_test.cpp.
 *
 * The original's regular expressions are matchers written out by hand here (speechtext.cpp
 * names the expression each one stands for); lengths are counted as JavaScript counts them.
 */
#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace fs {

/** A text as it should be read out in a language. */
std::string speakable(std::string_view text, std::string_view lang);

/**
 * A text in pieces a voice reads in one go: whole sentences, packed together
 * while they fit in `max` characters; a longer sentence cut at its commas,
 * and failing those at a space (or anywhere, in a script without spaces).
 * (`max` counts as `text.length` does: UTF-16 units.)
 */
std::vector<std::string> splitForSpeech(std::string_view text, int max = 180);

}  // namespace fs
