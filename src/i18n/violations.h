// SPDX-License-Identifier: AGPL-3.0-only
// Origin: src/i18n/violations.ts @ v1.7.7
/**
 * The rule behind each fault the instructor reports: the citation of the
 * German road traffic regulations (StVO), the same in every language.
 * An entry starting with "@" is a text key (where there is no paragraph).
 * Title and explanation are texts: "v.<id>.title", "v.<id>.why".
 */
#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "i18n/types.h"

namespace fs {

namespace detail {
const Record<std::string>& violationRefs();
}

/** fault id → citation, in the order of the original's table (ready whenever it is asked for, also while the program starts) */
inline const Record<std::string>& VIOLATION_REFS = detail::violationRefs();

struct CitationPart {
  std::string text;
  /** the page of the paragraph or annex this part cites */
  std::optional<std::string> url;
};

/**
 * A citation cut into pieces, each paragraph ("§ 8 Abs. 1a") and annex
 * ("Anlage 2") with the address of its text:
 * "§ 1 Abs. 2, § 7 Abs. 5 StVO" → [§ 1 Abs. 2 → …/__1.html] ", " [§ 7 Abs. 5 → …/__7.html] " StVO".
 */
std::vector<CitationPart> citationParts(std::string_view ref);

}  // namespace fs
