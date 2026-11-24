// SPDX-License-Identifier: AGPL-3.0-only
// Origin: src/i18n/violations.ts @ v1.7.7
#include "i18n/violations.h"

namespace fs {

namespace detail {

const Record<std::string>& violationRefs() {
  static const Record<std::string> REFS = {
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
      {"cyclistCutOff", "§ 9 Abs. 3 StVO"},
      {"doorCheck", "§ 14 Abs. 1 StVO"},
      // the tram: the regulation for trams (BOStrab) – § 50 the speeds, § 54 operation: doors closed before
      // departing (Abs. 1), opened only at stops and only once stopped (Abs. 2)
      {"tramCurve", "§ 50 BOStrab"},
      {"tramStopMissed", "§ 54 Abs. 2 BOStrab"},
      {"tramStopPlace", "§ 54 Abs. 2 BOStrab"},
      {"tramDoorsMoving", "§ 54 Abs. 1 BOStrab"},
      {"tramDoorsEarly", "§ 54 Abs. 1 BOStrab"},
      // the pantograph: the operating rules of the company (no paragraph of the BOStrab of its own)
      {"tramPantoLoad", "@ref.tramRules"},
      {"tramPantoWireEnd", "@ref.tramRules"},
      // on foot: the pavement (§ 25 Abs. 1), across the road straight and with an eye on the traffic – at a crossing
      // near by (§ 25 Abs. 3) –, the pedestrians' lights (§ 37 Abs. 2 Nr. 5)
      {"pedRed", "§ 37 Abs. 2 Nr. 5 StVO"},
      {"pedRoad", "§ 25 Abs. 1 StVO"},
      {"pedCrossing", "§ 25 Abs. 3 StVO"},
      {"pedForcedBrake", "§ 25 Abs. 3 StVO"},
      {"pedHit", "§ 25 Abs. 3 StVO"},
      // the bus on a line: the stops are the company's to serve (no paragraph of its own); pulling out of the stop is § 10 StVO
      {"busStopMissed", "@ref.busService"},
      {"busStopPlace", "@ref.busService"},
      {"busDoorsMoving", "@ref.busService"},
      {"busDoorsEarly", "@ref.busService"},
      {"busPullOut", "§ 10 StVO"},
      // a set route (game/routepresets.ts): the directions are the examiner's – no paragraph of their own
      {"routeLeft", "@ref.routePreset"},
      {"routeReverse", "@ref.routePreset"},
      {"classSpeed", "§ 3 Abs. 3, § 18 Abs. 5 StVO"},
      {"motorwayBan", "§ 18 Abs. 1 StVO"},
      {"doorTraffic", "§ 14 Abs. 1 StVO"},
      {"notSecured", "§ 14 Abs. 2 StVO"},
      {"preCheck", "§ 23 Abs. 1 StVO"},
      {"cyclistHit", "§ 1 Abs. 2, § 9 Abs. 3 StVO"},
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
      // a road shut at certain times: Zeichen 250, 251, 253, 260 with a time panel (Zusatzzeichen 1040/1042)
      {"accessTimed", "Anlage 2 StVO, Zeichen 250/260 mit Zusatzzeichen 1040-30"},
      {"distance", "§ 4 Abs. 1 StVO"},
      {"crash", "§ 1 Abs. 2 StVO"},
      {"crashObject", "§ 3 Abs. 1 StVO"},
      // class CE: the tasks of the practical test, and what the StVO asks of trucks
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
      // class B: the basic tasks of the practical test on the practice yard (src/game/drills.ts; Anlage 7 FeV, Nr. 2.1.4.2) –
      // the hill start is no basic task of class B: practice for the test drive (Nr. 2.1.5)
      {"drillKerbTouch", "Anlage 7 FeV, Nr. 2.1.4.2"},
      {"drillKerbOver", "Anlage 7 FeV, Nr. 2.1.4.2"},
      {"drillKerbFar", "Anlage 7 FeV, Nr. 2.1.4.2"},
      {"drillTouch", "§ 1 Abs. 2 StVO"},
      {"drillCorrections", "Anlage 7 FeV, Nr. 2.1.4.2"},
      {"drillLine", "Anlage 7 FeV, Nr. 2.1.4.2"},
      {"drillRollback", "§ 1 Abs. 2 StVO"},
      {"drillHillBrake", "Anlage 7 FeV, Nr. 2.1.5"},
      {"drillStall", "Anlage 7 FeV, Nr. 2.1.5"},
      {"drillBrakeSpeed", "Anlage 7 FeV, Nr. 2.1.4.2"},
      {"drillBrakeLate", "Anlage 7 FeV, Nr. 2.1.4.2"},
      {"drillBrakeSoft", "Anlage 7 FeV, Nr. 2.1.4.2"},
      {"drillBrakeLong", "Anlage 7 FeV, Nr. 2.1.4.2"},
      // (the articulated lorry: driving it so that the semi-trailer swings round against the cab – no command of the vehicle)
      {"semiJackknife", "Anlage 7 FeV, Nr. 2.1.4.5.2"},
      {"truckRightTurn", "§ 9 Abs. 6 StVO"},
      {"truckSpeed", "§ 3 Abs. 3, § 18 Abs. 5 StVO"},
      // roadworks and the closures for demonstrations (src/sim/roadevents.ts)
      {"worksLane", "Anlage 4 StVO, Zeichen 600, 605"},
      {"worksYield", "Anlage 2 StVO, Zeichen 208"},
      // a kerb build-out narrowing the road on the driver's side (src/sim/narrowings.ts)
      {"narrowYield", "§ 6 Satz 1 StVO"},
      // level crossings (src/sim/levelcrossing.ts): wait before the St Andrew's cross (§ 19 Abs. 2), not onto the tracks
      // without room behind them (Abs. 3), a moderate speed and no overtaking from the warning sign on (Abs. 1)
      {"levelCrossingRed", "§ 19 Abs. 2 Nr. 2 StVO"},
      {"levelCrossingYellow", "§ 19 Abs. 2 Nr. 2 StVO"},
      {"levelCrossingBarrier", "§ 19 Abs. 2 Nr. 3 StVO"},
      {"levelCrossingTrain", "§ 19 Abs. 1 Satz 1, Abs. 2 Nr. 1 StVO"},
      {"levelCrossingStop", "§ 19 Abs. 3, § 12 Abs. 1 Nr. 4 StVO"},
      {"levelCrossingOvertake", "§ 19 Abs. 1 Satz 3 StVO"},
      {"levelCrossingSpeed", "§ 19 Abs. 1 Satz 2 StVO"},
      {"closedRoad", "§ 36 Abs. 1 StVO; Anlage 2 StVO, Zeichen 250"},
      // emergency vehicles with blue lights and siren: make way at once (§ 38 Abs. 1), the corridor in slow traffic on a road
      // with two or more lanes in a direction (§ 11 Abs. 2) – src/sim/emergency.ts
      {"emergencyBlock", "§ 38 Abs. 1 StVO"},
      {"emergencyCorridor", "§ 11 Abs. 2 StVO"},
      {"emergencyJunction", "§ 38 Abs. 1 StVO"},
      {"stall", "@ref.vehicle"},
      {"seatbelt", "§ 21a Abs. 1 StVO"},
      // driving with the windscreen full of rain or snow (render/windscreen.ts): the driver must see out
      {"noView", "§ 23 Abs. 1 StVO"},
      // the lights and the main beam (src/sim/beams.ts): lights on in the dark and in poor sight, the main beam dipped in time
      {"dazzleOncoming", "§ 17 Abs. 2 StVO"},
      {"dazzleAhead", "§ 17 Abs. 2 StVO"},
      {"dazzleRider", "§ 17 Abs. 2 StVO"},
      {"beamLit", "§ 17 Abs. 2 StVO"},
      {"noLights", "§ 17 Abs. 1, Abs. 3 StVO"},
      {"handbrakeOn", "@ref.vehicle"},
      // working a crane (game/cranetasks.ts): its load chart and operating manual, the accident prevention regulation for cranes
      {"craneOverload", "@ref.crane"},
      {"craneLoadHit", "@ref.crane"},
      {"craneHookHit", "@ref.crane"},
      {"craneHardLanding", "@ref.crane"},
      {"craneSwing", "@ref.crane"},
      {"craneWind", "@ref.crane"},
      {"craneDiagonalPull", "@ref.crane"},
      {"craneNotLevel", "@ref.crane"},
      {"craneTipped", "@ref.crane"},
      // driving a forklift (game/forkliftplay.ts): its operating manual and load chart, the accident prevention regulation for industrial trucks
      {"forkliftTipped", "@ref.forklift"},
      {"forkliftOverload", "@ref.forklift"},
      {"forkliftForksIn", "@ref.forklift"},
      {"forkliftRearLift", "@ref.forklift"},
      {"forkliftTurnFast", "@ref.forklift"},
      {"forkliftHit", "@ref.forklift"},
      {"forkliftLoadHigh", "@ref.forklift"},
      {"forkliftTiltForward", "@ref.forklift"},
      {"forkliftForksHigh", "@ref.forklift"},
  };
  return REFS;
}

}  // namespace detail

namespace {

/** Where the regulations can be read: the official collection of federal law (gesetze-im-internet.de). */
const char STVO_URL[] = "https://www.gesetze-im-internet.de/stvo_2013/";
const char BOSTRAB_URL[] = "https://www.gesetze-im-internet.de/strabbo_1987/";
const char FEV_URL[] = "https://www.gesetze-im-internet.de/fev_2010/";
/** the criminal code (first aid: § 323c StGB, in the theory lessons) */
const char STGB_URL[] = "https://www.gesetze-im-internet.de/stgb/";

bool digit(std::string_view s, size_t i) { return i < s.size() && s[i] >= '0' && s[i] <= '9'; }
bool small(std::string_view s, size_t i) { return i < s.size() && s[i] >= 'a' && s[i] <= 'z'; }
/** \w at that place (a byte of a letter beyond ASCII is none, as in the original's expression) */
bool word(std::string_view s, size_t i) {
  return i < s.size() && ((s[i] >= '0' && s[i] <= '9') || (s[i] >= 'A' && s[i] <= 'Z') || (s[i] >= 'a' && s[i] <= 'z') || s[i] == '_');
}
bool at(std::string_view s, size_t i, std::string_view lit) { return s.substr(i, lit.size()) == lit; }

/** `\d+[a-z]?` from i on: where it ends (i: there is none) */
size_t number(std::string_view s, size_t i) {
  size_t j = i;
  while (digit(s, j)) j++;
  if (j == i) return i;
  if (small(s, j)) j++;
  return j;
}

struct Match {
  size_t length = 0;
  /** m[1]: the number of the paragraph ("8", "21a") */
  std::string paragraph;
  /** m[2], m[3]: the number of the annex and the regulation it belongs to */
  std::string annex, annexLaw;
};

/** /§ (\d+[a-z]?)(?: Abs\. \d+[a-z]?(?:, Abs\. \d+[a-z]?)*)?|Anlage (\d+)(?= (StVO|FeV))/ at position i */
bool matchAt(std::string_view s, size_t i, Match& m) {
  static const std::string_view PARAGRAPH = "§ ";
  if (at(s, i, PARAGRAPH)) {
    const size_t from = i + PARAGRAPH.size();
    size_t j = number(s, from);
    if (j > from) {
      m.paragraph = std::string(s.substr(from, j - from));
      static const std::string_view ABS = " Abs. ", MORE = ", Abs. ";
      if (at(s, j, ABS)) {
        size_t k = number(s, j + ABS.size());
        if (k > j + ABS.size()) {
          j = k;
          while (at(s, j, MORE)) {
            k = number(s, j + MORE.size());
            if (k == j + MORE.size()) break;
            j = k;
          }
        }
      }
      m.length = j - i;
      return true;
    }
  }
  static const std::string_view ANNEX = "Anlage ";
  if (at(s, i, ANNEX)) {
    const size_t from = i + ANNEX.size();
    size_t j = from;
    while (digit(s, j)) j++;
    // (an annex belongs to the regulation named behind it: the StVO or the driving-licence regulation FeV)
    if (j > from && (at(s, j, " StVO") || at(s, j, " FeV"))) {
      m.annex = std::string(s.substr(from, j - from));
      m.annexLaw = at(s, j, " StVO") ? "StVO" : "FeV";
      m.length = j - i;
      return true;
    }
  }
  return false;
}

/** /\b(StVO|FeV|BOStrab|StGB)\b/.exec(s)?.[1] */
std::string lawNamed(std::string_view s) {
  static const std::string_view LAWS[] = {"StVO", "FeV", "BOStrab", "StGB"};
  for (size_t i = 0; i < s.size(); i++) {
    if (i > 0 && word(s, i - 1)) continue;
    for (std::string_view law : LAWS)
      if (at(s, i, law) && !word(s, i + law.size())) return std::string(law);
  }
  return "";
}

}  // namespace

std::vector<CitationPart> citationParts(std::string_view ref) {
  std::vector<CitationPart> parts;
  size_t last = 0;
  for (size_t i = 0; i < ref.size();) {
    Match m;
    if (!matchAt(ref, i, m)) {
      i++;
      continue;
    }
    if (i > last) parts.push_back({std::string(ref.substr(last, i - last)), std::nullopt});
    std::string url;
    if (!m.paragraph.empty()) {
      // (a paragraph belongs to the law named after it: the StVO, the tram regulation BOStrab, the criminal code StGB)
      std::string law = lawNamed(ref.substr(i));
      if (law.empty()) law = "StVO";
      url = std::string(law == "BOStrab" ? BOSTRAB_URL : law == "StGB" ? STGB_URL : STVO_URL) + "__" + m.paragraph + ".html";
    } else {
      url = std::string(m.annexLaw == "FeV" ? FEV_URL : STVO_URL) + "anlage_" + m.annex + ".html";
    }
    parts.push_back({std::string(ref.substr(i, m.length)), url});
    last = i + m.length;
    i = last;
  }
  if (last < ref.size()) parts.push_back({std::string(ref.substr(last)), std::nullopt});
  return parts;
}

}  // namespace fs
