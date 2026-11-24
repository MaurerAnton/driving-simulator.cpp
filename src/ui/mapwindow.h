// SPDX-License-Identifier: AGPL-3.0-only
// Origin: src/ui/mapwindow.ts @ v1.7.7
/**
 * The windows on the big map (the panel for planning a drive, the small
 * windows of a stop or a vehicle): as large as what is in them, without
 * scrollbars to be seen (they still scroll when taller than the screen), and
 * the player can make them smaller – fold one up to its title with the
 * button beside it, or drag the grip at its lower corner (a finger or the
 * mouse; a double tap on the grip gives it its own size back).
 *
 * Their looks are in maps.css (web/css, linked by the page).
 */
#pragma once

#include <functional>
#include <string>

#include "platform/dom.h"

namespace fs {

/** A button in the window's title row that folds it up to that row, and unfolds it again. */
dom::El foldButton(const dom::El& panel, const dom::El& head, std::function<std::string(bool folded)> label);

/** the least a window can be made (px) */
struct GripMin {
  double w = 220;
  double h = 96;
};

/**
 * A grip at the lower corner of a window (one that lies in a box of its own,
 * at the top and the start of it): dragging it makes the window narrower or
 * lower than what is in it needs.  Returns what takes it away again.
 */
std::function<void()> addGrip(const dom::El& panel, const std::string& label, GripMin min = {});

}  // namespace fs
