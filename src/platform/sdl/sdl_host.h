// SPDX-License-Identifier: AGPL-3.0-only
/**
 * The game in a window of its own (experimental, 2.1): SDL3 makes the window, an OpenGL
 * ES 3 context, the loudspeakers and the hands (keys, mouse, fingers, gamepads); everything
 * else is the native host (platform/native: files, the clock, the page in memory, the sound
 * graphs).  The renderer draws as it does off screen – into its own framebuffer, the
 * "canvas's drawing buffer" – and the window shows that, stretched to the window as a
 * browser stretches the canvas, with the interface's picture over it (platform/overlay.h).
 *
 * Only SDL's portable calls are used: Android follows through SDL's port.
 *
 * What goes where:
 *   keys        → keydown / keyup at the page's body (bubbling to window), with the browser's
 *                 `code` (KeyW, ArrowUp, Space, ShiftLeft …) and `key`
 *   the mouse   → pointerdown / pointermove / pointerup (pointerType 'mouse'), wheel, dblclick,
 *                 contextmenu at the canvas
 *   fingers     → pointer events (pointerType 'touch') and touchstart / touchmove / touchend
 *   gamepads    → navigator.getGamepads() (plat::getGamepads: the 'standard' layout)
 *   the window  → innerWidth / innerHeight / devicePixelRatio and the 'resize' event; focus
 *                 lost → 'blur'
 *   the sound   → audioNative::mix() into an SDL audio stream
 */
#pragma once

#include <string>

#include "platform/dom.h"

namespace fs::sdl {

struct Options {
  std::string title = "Fahrsim Baunatal 2";
  /** the window's size (points) */
  int width = 1280, height = 720;
  /** wait for the screen's refresh before each picture */
  bool vsync = true;
  /** play the sound */
  bool audio = true;
};

/**
 * Open the window and its OpenGL ES 3 context; the native host draws with it from now
 * on (plat::gl::createContext only makes the game's framebuffer).  False: no window can be
 * had here (no display – `why` says what SDL said).
 */
bool open(const Options& options, std::string* why = nullptr);
/** the element the hands are sent to (the game's canvas: it is put in the page's body) */
void attach(const dom::El& canvas);
/**
 * One turn of the loop: the window's events, what is due in the host's queue, the pictures
 * asked for (requestAnimationFrame), the sound, the frame shown.  False: the window was closed.
 */
bool frame();
/** how many requestAnimationFrame callbacks ran so far, and in how many turns of frame() some ran */
long callbacksRun();
long picturesShown();
/** the game's last frame (without what is drawn over it) as a PNG file */
bool screenshot(const std::string& path);
/** which driver draws, and which of SDL's video drivers is used ("wayland", "x11", "offscreen" …) */
std::string rendererName();
std::string videoDriver();
/** close the window, the context, the sound */
void close();

}  // namespace fs::sdl
