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
#include "sim/signtime.h"

namespace fs {


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
