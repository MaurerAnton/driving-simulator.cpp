// SPDX-License-Identifier: AGPL-3.0-only
/**
 * The rule behind each fault: the German citation, as in
 * src/i18n/violations.ts (MaurerAnton/driving-simulator @ 408014b, v0.6.0).
 */
#include "i18n/violations.h"

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
    {"stall", "@ref.vehicle"},
    {"seatbelt", "§ 21a Abs. 1 StVO"},
    {"handbrakeOn", "@ref.vehicle"},
};

}  // namespace fs
