// SPDX-License-Identifier: AGPL-3.0-only
// Origin: src/ui/closex.ts @ v1.7.7
/**
 * The red × that closes a panel, in its upper right corner.
 *
 * Every close button of the game looks the same: a round red button with a
 * white × (style.css `.x-close` – the big map's, the mirror's, the main
 * menu's sheets and the map's pop-ups carry the same look by their own
 * classes).  On a panel it stays in the corner while the panel scrolls.
 */
#pragma once

#include <functional>
#include <string>

#include "platform/dom.h"

namespace fs {

/** A red × for the corner of a panel: `label` says what it does (for screen readers and as its tooltip). */
dom::El closeX(const std::string& label, std::function<void()> act);

}  // namespace fs
