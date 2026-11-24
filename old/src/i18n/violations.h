// SPDX-License-Identifier: AGPL-3.0-only
/**
 * The rule behind each fault the instructor reports: the citation of the
 * German road traffic regulations (StVO), the same in every language.
 * An entry starting with "@" is a text key (where there is no paragraph).
 * Title and explanation are texts: "v.<id>.title", "v.<id>.why".
 *
 * Origin: src/i18n/violations.ts (MaurerAnton/driving-simulator @ ff37c5d, v0.7.0).
 */
#pragma once

#include <string>
#include <utility>
#include <vector>

namespace fs {

extern const std::vector<std::pair<std::string, std::string>> VIOLATION_REFS;

struct CitationPart {
  std::string text;
  /** the page of the paragraph or annex this part cites ("" when none) */
  std::string url;
};

/**
 * A citation cut into pieces, each paragraph ("§ 8 Abs. 1a") and annex
 * ("Anlage 2") with the address of its text:
 * "§ 1 Abs. 2, § 7 Abs. 5 StVO" → [§ 1 Abs. 2 → …/__1.html] ", " [§ 7 Abs. 5 → …/__7.html] " StVO".
 */
std::vector<CitationPart> citationParts(const std::string& ref);

}  // namespace fs
