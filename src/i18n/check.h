// SPDX-License-Identifier: AGPL-3.0-only
/**
 * Checks the translations against the English reference (the original runs
 * this as `npm run i18n:check`; here the test runs it against every
 * language, and it is available to the tools):
 *
 *   A language is complete when it has exactly the keys of en, every text
 *   keeps its {placeholders} and [[key]] tokens, the names and numbers that
 *   must not change are still there, and the short labels are short.
 *
 * Origin: tools/i18n-check.ts (MaurerAnton/driving-simulator @ 408014b,
 * v0.6.0), with the rules in docs/TRANSLATING.md.
 */
#pragma once

#include <string>
#include <vector>

#include "i18n/index.h"

namespace fs {

struct Report {
  std::vector<std::string> errors;
  std::vector<std::string> warnings;
};

/**
 * Check one language's texts against the reference (English unless given).
 * `code` decides the script rules (a language with its own script must be
 * written in it).
 */
Report checkDict(const std::string& code, const Dict& dict, const Dict& ref);

/** The same with the built-in English as the reference. */
Report checkDict(const std::string& code, const Dict& dict);

}  // namespace fs
