// SPDX-License-Identifier: AGPL-3.0-only
// Origin: src/ui/hud.ts @ v1.7.7
/** What the files of the Hud share (hud.cpp, hud_menu.cpp, hud_settings.cpp, hud_touch.cpp) – not for anybody else. */
#pragma once

#include <functional>
#include <string>
#include <utility>
#include <vector>

#include "core/task.h"
#include "i18n/index.h"
#include "ui/addresspicker.h"
#include "platform/dom.h"
#include "sim/rules.h"

namespace fs {

// --- needs from the host -------------------------------------------------------------
// Two things the original asks of the browser that no header of the port has yet.  They
// are declared here as they should be, so that the Hud says what it means; the program
// links once the host (and game/netstats.h, whose function the second is) has them.

namespace plat {
/** window.confirm(text): a question the player answers with OK (true) or Cancel */
bool confirm(const std::string& text);
}  // namespace plat

/** game/netstats.ts `downloaded()`: the bytes fetched since the page opened (the browser's resource timing) */
double downloaded();

namespace hudParts {

/** the original's `el(tag, cls, html)` where it is given markup (dom::el takes a text) */
dom::El html(const char* tag, const std::string& cls, const std::string& markup);

/** `el('h1', '', 'Fahrsim <span>Baunatal</span>')` */
dom::El title();

/** the values of a fault (sim/rules.h Violation::data, game/faultlog.h FaultEntry::data) as tr() takes them */
TrParams params(const ViolationData& data);

/** `text.split(/\s+/).filter(Boolean)` */
std::vector<std::string> words(const std::string& text);

/**
 * `new Date(text)` for the moments the game writes: "2026-11-21T19:27:22Z" (UTC) and
 * "2026-11-23T12:00:00" (the reader's own time) – NaN for anything else.
 */
double dateOf(const std::string& text);

/** `load: () => this.loadAddresses()` as the address field takes it: the search of the list, once it is there */
Task<AddressSearchOf<AddressHit>> searchOf(std::function<Task<AddressIndex*>()> load);

/** call a hook of the Hud (a copy of it: what it does may put another in its place) */
template <class F, class... Args>
void run(const F& hook, Args&&... args) {
  const F copy = hook;
  copy(std::forward<Args>(args)...);
}

}  // namespace hudParts

}  // namespace fs
