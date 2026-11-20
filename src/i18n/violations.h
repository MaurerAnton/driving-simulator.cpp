// SPDX-License-Identifier: AGPL-3.0-only
/**
 * The rule behind each fault the instructor reports: the citation of the
 * German road traffic regulations (StVO), the same in every language.
 * An entry starting with "@" is a text key (where there is no paragraph).
 * Title and explanation are texts: "v.<id>.title", "v.<id>.why".
 *
 * Origin: src/i18n/violations.ts (MaurerAnton/driving-simulator @ 408014b, v0.6.0).
 */
#pragma once

#include <string>
#include <utility>
#include <vector>

namespace fs {

extern const std::vector<std::pair<std::string, std::string>> VIOLATION_REFS;

}  // namespace fs
