// SPDX-License-Identifier: AGPL-3.0-only
/**
 * The page in memory (the native host): what a test does with it – read what
 * the interface built, click, type, press keys.
 */
#pragma once

#include <string>

#include "platform/dom.h"

namespace fs::dom::native {

/** empty the page (the body's children, every handler on window, document and body) */
void reset();
/** the element and what is in it, written as HTML (`pretty`: one element a line, indented) */
std::string html(const El& e, bool pretty = false);
/** textContent */
std::string text(const El& e);
/** what getBoundingClientRect answers for it (nothing is laid out here) */
void setRect(const El& e, Rect r);
/** send an event to an element (through its ancestors, as a browser does); false: a handler prevented the default */
bool dispatch(const El& target, Event& e, bool bubbles = true);
bool dispatch(const El& target, const std::string& type);
/** type into a field: its value, then the 'input' event */
void type(const El& field, const std::string& value);
/** choose an option of a select: its value, then the 'change' event */
void choose(const El& select, const std::string& value);
/** press or release a key (it goes to the focused element, else to the body, and up to the window) */
bool key(const std::string& code, const std::string& keyText = "", bool down = true);
/** the innermost element under `within` that matches `selector` and whose text contains `textPart` (empty: none) */
El find(const El& within, const std::string& textPart, const std::string& selector = "*");

}  // namespace fs::dom::native
