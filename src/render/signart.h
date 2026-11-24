// SPDX-License-Identifier: AGPL-3.0-only
/**
 * Traffic signs drawn by the program: those of the official catalogue
 * (Verkehrszeichenkatalog, VzKat) that have no graphic in
 * public/signs/signs.json, and those whose plate shows a value only the map
 * knows – the time of a panel ("22–6 h", "Mo–Fr 7–17 h"), a mass, a width,
 * a distance.  German traffic signs are official works and in the public
 * domain (§ 5 Abs. 1 UrhG); they are drawn here from their description, in
 * their colours and shapes, simplified where a pictogram is involved.
 *
 * A code is a catalogue number ("1026-37", "257-54"), with what the plate
 * shows after the first colon: "1040-30:22:00-06:00" (a time as
 * OpenStreetMap writes it, src/sim/signtime), "262:7.5", "1004-30:150".
 * `drawSignArt` draws one into a rectangle of the atlas and says whether it
 * knew the sign; `artAspect` gives the width / height of its plate.
 *
 * Origin: src/render/signart.ts @ v1.7.7
 */
#pragma once

#include <functional>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "platform/canvas.h"

namespace fs {

// ---- src/sim/signtime.ts (not translated yet) ---------------------------------------------
// What this file asks of it, by the original's names.  The two functions are null until
// that file is there – a panel with a time then shows it as the map writes it
// ("22:00-06:00"), which is what the original shows for a time it cannot read.  When
// sim/signtime is translated these declarations go and its own take their place.

/**
 * One span of a time window: on which days (bits 0–6 Sunday … Saturday, bit 7:
 * public holidays) it begins, from when to when (minutes after midnight of
 * that day; `from` < `to`).
 */
struct TimeSpan {
  int days = 0;
  double from = 0;
  double to = 0;
};

/** When something holds: in one of its spans, and not on the days it is off. */
struct TimeWindow {
  std::vector<TimeSpan> spans;
  int off = 0;
};

/** Read a time as conditions and panels write it ("Mo-Fr 07:00-17:00"); none for anything else. */
extern std::function<std::optional<TimeWindow>(const std::string& raw)> parseTimeWindow;
/** The lines of a panel for a window, as German panels write them: ["Mo–Fr", "7–17 h"]. */
extern std::function<std::vector<std::string>(const TimeWindow& w)> windowLines;

// -------------------------------------------------------------------------------------------

/** "1040-30:22:00-06:00" → ["1040-30", "22:00-06:00"] (only the first colon parts them) */
std::pair<std::string, std::string> splitCode(const std::string& code);

/** The width / height of the plate of a sign drawn here (none: not drawn here). */
std::optional<double> artAspect(const std::string& code);

/**
 * Draw sign `code` into the rectangle (x, y, w, h) – its plate fills it –
 * and say whether this file knows it.
 */
bool drawSignArt(const Ctx2D& g, const std::string& code, double x, double y, double w, double h);

}  // namespace fs
