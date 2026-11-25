// SPDX-License-Identifier: AGPL-3.0-only
/**
 * The native OpenGL host (gl_native.cpp) for a host that brings its own context – a
 * window (platform/sdl): the game still draws into its own framebuffer (the "canvas's
 * drawing buffer", of the size the renderer asks for), and the window shows it.
 */
#pragma once

#include <cstdint>

namespace fs::plat::gl::native {

/**
 * The OpenGL ES 3 context that is current now is the one to draw with: createContext()
 * makes no EGL context of its own from here on, only the framebuffer the game draws into.
 */
void adoptCurrentContext();
/** that framebuffer and its size in pixels (0: none yet) */
void drawingBuffer(uint32_t& fbo, int& width, int& height);

}  // namespace fs::plat::gl::native
