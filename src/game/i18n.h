// SPDX-License-Identifier: AGPL-3.0-only
/**
 * Texts (German / English) and the catalogue of rule violations with their
 * legal basis.
 *
 * Origin: src/ui/i18n.ts (MaurerAnton/driving-simulator @ 5b6ddc7, v0.1.0).
 * The strings are carried over verbatim, including the {param} placeholders.
 */
#pragma once

#include <string>
#include <utility>
#include <vector>

namespace fs {

enum class Lang { De = 0, En = 1 };

/** [German, English] pair, as in the TypeScript `type Pair`. */
using Pair = std::pair<std::string, std::string>;

/** All UI strings, keyed like the TypeScript `UI` record. */
extern const std::vector<std::pair<std::string, Pair>> UI_ENTRIES;

/** What the rule says and to do instead, plus the legal basis. */
struct ViolationText {
  Pair title;
  Pair why;
  std::string ref;
};

extern const std::vector<std::pair<std::string, ViolationText>> VIOLATION_ENTRIES;

/** Look up a UI string; unknown keys return the key itself (as `tr` does). */
std::string tr(const std::string& key, Lang lang);

/** Substitute {name} placeholders in order (same left-to-right behaviour as JS). */
std::string tr(const std::string& key, Lang lang,
               const std::vector<std::pair<std::string, std::string>>& params);

std::string violationTitle(const std::string& id, Lang lang,
                           const std::vector<std::pair<std::string, std::string>>& data);
std::string violationWhy(const std::string& id, Lang lang);

}  // namespace fs
