// SPDX-License-Identifier: AGPL-3.0-only
/**
 * What is printed on the keys the simulator uses.
 *
 * The controls go by the position of a key (KeyboardEvent.code): the four
 * keys under the left hand steer and accelerate whatever is printed on them –
 * W A S D on a US keyboard, Z Q S D on a French one.  The texts therefore
 * name keys by their code ([[KeyW]]), and this module turns the code into
 * the letter of the player's own keyboard: from the browser where it tells
 * (Chromium: navigator.keyboard.getLayoutMap), otherwise from the layout that
 * is usual for the language.
 *
 * Origin: src/i18n/keys.ts (MaurerAnton/driving-simulator @ 408014b, v0.6.0).
 */
#pragma once

#include <functional>
#include <map>
#include <string>

namespace fs {

/**
 * Ask the browser for the keyboard layout (once, at start; harmless if it does
 * not know).  The platform reports it as code → the letter printed there; an
 * empty map keeps the layout that is usual for the language.
 */
void setKeyLayout(std::map<std::string, std::string> layout);

/**
 * The label of a key.  `name` gives the translated names of the keys that
 * carry a word (space bar, Enter, Shift, Esc).
 */
std::string keyLabel(const std::string& code, const std::string& lang,
                     const std::function<std::string(const std::string&)>& name);

/**
 * The same on a touch screen: the control on the screen that does what the
 * key does.  Values starting with "@" are text keys (labels of the "⋯" sheet).
 */
std::string touchLabel(const std::string& code, const std::string& lang,
                       const std::function<std::string(const std::string&)>& name);

}  // namespace fs
