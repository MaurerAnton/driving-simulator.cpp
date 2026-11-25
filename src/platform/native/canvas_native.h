// SPDX-License-Identifier: AGPL-3.0-only
/** The 2D canvas of the native host: what a test asks about a context, and its pixels. */
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "platform/canvas.h"

namespace fs::canvasNative {

/** how many drawing calls the context has taken */
long calls(const Ctx2D& g);
/** the texts it was asked to write (the last few hundred) */
std::vector<std::string> texts(const Ctx2D& g);
/** start counting anew */
void forget(const Ctx2D& g);

/** changes whenever the context paints (to tell whether its pixels must be taken again) */
uint64_t version(const Ctx2D& g);
/**
 * The pixels of a canvas (element handle) as RGBA bytes, the top row first (`flipY`: the
 * bottom row first), premultiplied by alpha or not.  A canvas never painted on is
 * transparent.  false: the canvas has no size.
 */
bool pixels(int canvasElement, std::vector<uint8_t>& rgba, int& width, int& height, bool premultiplied = false, bool flipY = false);
/** the same of a picture loadImage/decodeImage made (an SVG one at its own size); false: there is no such picture */
bool imagePixels(int image, std::vector<uint8_t>& rgba, int& width, int& height, bool premultiplied = false, bool flipY = false);
/** what the canvas holds, as a PNG file */
bool savePng(const Canvas& canvas, const std::string& path);

/** (platform/native/dom_native.cpp) the canvas was given a width or height: it is blank, its context as new */
void resized(int canvasElement);
/** (dom_native.cpp) nobody holds the canvas any more: its pixels go */
void released(int canvasElement);

}  // namespace fs::canvasNative
