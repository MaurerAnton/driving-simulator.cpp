// SPDX-License-Identifier: AGPL-3.0-only
// Origin: src/i18n/keys.ts @ v1.7.7
/**
 * What is printed on the keys the simulator uses.
 *
 * The controls go by the position of a key (KeyboardEvent.code): the four
 * keys under the left hand steer and accelerate whatever is printed on them –
 * W A S D on a US keyboard, Z Q S D on a French one.  The texts therefore
 * name keys by their code ([[KeyW]]), and this module turns the code into
 * the letter of the player's own keyboard: from the browser where it tells
 * (Chromium: navigator.keyboard.getLayoutMap – plat::keyboardLayoutMap), otherwise
 * from the layout that is usual for the language.
 */
#pragma once

#include <functional>
#include <string>
#include <string_view>

#include "core/task.h"

namespace fs {

/** Ask the browser for the keyboard layout (once, at start; harmless if it does not know). */
Task<void> initKeyLabels();

/** `(key: string) => string`: the text of a key name ("key.Space") in the language at hand */
using KeyNameFn = std::function<std::string(std::string_view key)>;

/**
 * The label of a key.  `name` gives the translated names of the keys that
 * carry a word (space bar, Enter, Shift, Esc).
 */
std::string keyLabel(std::string_view code, std::string_view lang, const KeyNameFn& name);

/**
 * The same on a touch screen: the control on the screen that does what the
 * key does.
 */
std::string touchLabel(std::string_view code, std::string_view lang, const KeyNameFn& name);

}  // namespace fs
