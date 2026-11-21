// SPDX-License-Identifier: AGPL-3.0-only
/**
 * The rule behind each fault: the German citation, as in
 * src/i18n/violations.ts (MaurerAnton/driving-simulator @ ff37c5d, v0.7.0).
 */
#include "i18n/violations.h"

#include <algorithm>
#include <cstdint>

namespace fs {

const std::vector<std::pair<std::string, std::string>> VIOLATION_REFS = {
    {"speed", "§ 3 StVO, Zeichen 274"},
    {"speedWalk", "Anlage 3 StVO, Zeichen 325.1"},
    {"redLight", "§ 37 Abs. 2 StVO"},
    {"stopLine", "Anlage 2 StVO, Zeichen 294; § 37 Abs. 2 StVO"},
    {"amberLight", "§ 37 Abs. 2 StVO"},
    {"stopSign", "Anlage 2 StVO, Zeichen 206"},
    {"prioritySign", "§ 8 Abs. 2 StVO, Zeichen 205/206"},
    {"rightBeforeLeft", "§ 8 Abs. 1 StVO"},
    {"priorityRoundabout", "§ 8 Abs. 1a StVO"},
    {"priorityDriveway", "§ 10 StVO"},
    {"leftTurnOncoming", "§ 9 Abs. 3 StVO"},
    {"cutOff", "§ 1 Abs. 2, § 7 Abs. 5, § 10 StVO"},
    {"zebra", "§ 26 Abs. 1 StVO, Zeichen 350"},
    {"zebraSpeed", "§ 26 Abs. 1 StVO"},
    {"pedestrian", "§ 1 Abs. 2, § 3 Abs. 2a StVO"},
    {"noIndicator", "§ 9 Abs. 1 StVO"},
    {"wrongIndicator", "§ 9 Abs. 1 StVO"},
    {"indicatorLeftOn", "§ 9 Abs. 1 StVO"},
    {"noShoulderCheck", "§ 9 Abs. 1, Abs. 3 StVO"},
    {"roundaboutEntryIndicator", "§ 8 Abs. 1a StVO"},
    {"roundaboutExitIndicator", "§ 8 Abs. 1a, § 9 Abs. 1 StVO"},
    {"turnBan", "Anlage 2 StVO, Zeichen 209–214"},
    {"wrongWay", "Anlage 2 StVO, Zeichen 220, 267"},
    {"keepRight", "§ 2 Abs. 2 StVO"},
    {"sidewalk", "§ 2 Abs. 1 StVO"},
    {"offRoad", "§ 2 Abs. 1 StVO"},
    {"accessPrivate", "Anlage 2 StVO, Zeichen 250"},
    {"accessAgricultural", "Anlage 2 StVO, Zeichen 250/260"},
    {"accessDestination", "Zusatzzeichen 1020-30"},
    {"distance", "§ 4 Abs. 1 StVO"},
    {"crash", "§ 1 Abs. 2 StVO"},
    {"crashObject", "§ 3 Abs. 1 StVO"},
    {"ceLine", "Anlage 7 FeV, Nr. 2.1.4.5"},
    {"ceCorrections", "Anlage 7 FeV, Nr. 2.1.4.5"},
    {"ceRamp", "Anlage 7 FeV, Nr. 2.1.4.5"},
    {"ceSecure", "Anlage 7 FeV, Nr. 2.1.3"},
    {"ceAir", "Anlage 7 FeV, Nr. 2.1.3"},
    {"ceOpen", "Anlage 7 FeV, Nr. 2.1.3"},
    {"ceLocked", "Anlage 7 FeV, Nr. 2.1.3"},
    {"ceRelease", "Anlage 7 FeV, Nr. 2.1.3"},
    {"ceLights", "Anlage 7 FeV, Nr. 2.1.3"},
    {"ceHeight", "Anlage 7 FeV, Nr. 2.1.3"},
    {"ceFast", "Anlage 7 FeV, Nr. 2.1.3"},
    {"truckRightTurn", "§ 9 Abs. 6 StVO"},
    {"truckSpeed", "§ 3 Abs. 3, § 18 Abs. 5 StVO"},
    {"stall", "@ref.vehicle"},
    {"seatbelt", "§ 21a Abs. 1 StVO"},
    {"handbrakeOn", "@ref.vehicle"},
};

namespace {
/** Where the regulations can be read: the official collection of federal law. */
const char* const STVO_URL = "https://www.gesetze-im-internet.de/stvo_2013/";
const char* const FEV_URL = "https://www.gesetze-im-internet.de/fev_2010/";
}  // namespace

std::vector<CitationPart> citationParts(const std::string& ref) {
  std::vector<CitationPart> parts;
  // (an annex belongs to the regulation named behind it: the StVO or the
  // driving-licence regulation FeV)
  // /§ (\d+[a-z]?)(?: Abs\. \d+[a-z]?(?:, Abs\. \d+[a-z]?)*)?|Anlage (\d+)(?= (StVO|FeV))/g
  size_t i = 0, last = 0;
  while (i < ref.size()) {
    // find the next "§ " or "Anlage "
    size_t at = ref.find("\xc2\xa7 ", i);          // § (utf-8 c2 a7) + space
    size_t an = ref.find("Anlage ", i);
    size_t pos = std::min(at == std::string::npos ? SIZE_MAX : at,
                          an == std::string::npos ? SIZE_MAX : an);
    if (pos == SIZE_MAX) break;
    bool isSection = (pos == at);
    size_t match = 0;
    if (isSection) {
      // § \d+[a-z]? ( Abs\. \d+[a-z]? (, Abs\. \d+[a-z]?)* )?
      size_t j = pos + 3;   // after "§ "
      if (j >= ref.size() || ref[j] < '0' || ref[j] > '9') { i = pos + 2; continue; }
      while (j < ref.size() && ref[j] >= '0' && ref[j] <= '9') j++;
      if (j < ref.size() && ref[j] >= 'a' && ref[j] <= 'z') j++;
      // ( Abs. \d+[a-z]? (, Abs. \d+[a-z]?)* )?
      auto absGroup = [&](size_t at) -> size_t {
        // returns the end of an " Abs. <n>" group at `at`, or `at` when none
        const std::string needle = " Abs. ";
        if (ref.compare(at, needle.size(), needle) != 0) return at;
        size_t k = at + needle.size();
        if (k >= ref.size() || ref[k] < '0' || ref[k] > '9') return at;
        while (k < ref.size() && ref[k] >= '0' && ref[k] <= '9') k++;
        if (k < ref.size() && ref[k] >= 'a' && ref[k] <= 'z') k++;
        return k;
      };
      {
        size_t k = absGroup(j);
        if (k != j) {
          j = k;
          // (, Abs. \d+[a-z]?)*
          for (;;) {
            const std::string sep = ", Abs. ";
            if (ref.compare(j, sep.size(), sep) != 0) break;
            size_t m = j + sep.size();
            if (m >= ref.size() || ref[m] < '0' || ref[m] > '9') break;
            while (m < ref.size() && ref[m] >= '0' && ref[m] <= '9') m++;
            if (m < ref.size() && ref[m] >= 'a' && ref[m] <= 'z') m++;
            j = m;
          }
        }
      }
      match = j;
      std::string text = ref.substr(pos, match - pos);
      if (pos > last) parts.push_back({ref.substr(last, pos - last), ""});
      // the number of the paragraph: the digits right after "§ " (two bytes)
      std::string n;
      for (size_t q = pos + 3; q < match && ref[q] >= '0' && ref[q] <= '9'; q++) n += ref[q];
      parts.push_back({text, std::string(STVO_URL) + "__" + n + ".html"});
      last = match;
      i = match;
    } else {
      // Anlage \d+ before " StVO" or " FeV"
      size_t j = pos + 7;
      if (j >= ref.size() || ref[j] < '0' || ref[j] > '9') { i = pos + 7; continue; }
      size_t numStart = j;
      while (j < ref.size() && ref[j] >= '0' && ref[j] <= '9') j++;
      std::string num = ref.substr(numStart, j - numStart);
      // lookahead: (StVO|FeV)
      size_t sp = j;
      bool ok = false;
      std::string reg;
      if (sp < ref.size() && ref[sp] == ' ') {
        if (ref.compare(sp + 1, 4, "StVO") == 0) { ok = true; reg = "StVO"; }
        else if (ref.compare(sp + 1, 3, "FeV") == 0) { ok = true; reg = "FeV"; }
      }
      if (!ok) { i = pos + 7; continue; }
      // the lookahead consumes nothing: the match ends after the digits
      size_t match = j;
      if (pos > last) parts.push_back({ref.substr(last, pos - last), ""});
      std::string base = reg == "FeV" ? FEV_URL : STVO_URL;
      parts.push_back({ref.substr(pos, match - pos), base + "anlage_" + num + ".html"});
      last = match;
      i = match;
    }
  }
  if (last < ref.size()) parts.push_back({ref.substr(last), ""});
  return parts;
}

}  // namespace fs
