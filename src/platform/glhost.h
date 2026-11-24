// SPDX-License-Identifier: AGPL-3.0-only
/**
 * What the renderer asks of the host about OpenGL: a context (WebGL 2 on the
 * page's canvas; OpenGL ES 3 off screen natively), and the pixels of things only
 * the host has – a canvas of the page, a picture it decoded.
 */
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace fs::plat::gl {

/** What kind of context the game asks for (the settings decide before the first frame). */
struct ContextOptions {
  /** smooth edges (the browser's multisampling of the canvas) */
  bool antialias = true;
  /** prefer the strong graphics card where there are two */
  bool highPerformance = true;
};

/**
 * Make the context and make it current.  In the browser: on the canvas with this
 * element id.  Natively: an off-screen one of the given size (pixels).  False: no
 * context can be had (the page says so; a native tool goes on without pictures).
 */
bool createContext(const std::string& canvasId, int width, int height, const ContextOptions& options = ContextOptions());
bool hasContext();
/** the canvas's drawing buffer gets this size in real pixels (natively: the off-screen picture) */
void resizeDrawingBuffer(int width, int height);
/** the framebuffer that is "the screen" (0 in the browser; the off-screen one natively) */
uint32_t screenFramebuffer();

/**
 * glTexImage2D of the bound 2D texture from a canvas of the page (its element's
 * handle).  `flipY`: the canvas's top row becomes the texture's top (three.js's
 * flipY).  Fills in the size; false: the canvas has no pixels here (natively) –
 * nothing was uploaded.
 */
bool texImageCanvas(int canvasElement, bool flipY, bool srgb, int& width, int& height);
/** the same from a picture the host decoded (plat images) */
bool texImageImage(int image, bool flipY, bool srgb, int& width, int& height);

/** natively: the picture drawn so far as a PNG file (false: could not be written); in the browser: not available */
bool savePng(const std::string& path, int width, int height);
/** which driver draws ("Mali-G52", "llvmpipe" …; "none"; in the browser the WebGL renderer string) */
std::string rendererName();

}  // namespace fs::plat::gl
