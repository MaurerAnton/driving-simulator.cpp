// SPDX-License-Identifier: AGPL-3.0-only
/** platform/glhost.h in the browser: a WebGL 2 context on the page's canvas. */
#include <emscripten/html5.h>

#include <cstdlib>
#include <string>

#include "platform/glhost.h"

extern "C" {
int fsg_tex_image(int fromCanvas, int source, int flipY, int srgb, int* sizeOut);
char* fsg_renderer_name();
}

namespace fs::plat::gl {

namespace {
EMSCRIPTEN_WEBGL_CONTEXT_HANDLE context = 0;
std::string canvasSelector;
}  // namespace

bool createContext(const std::string& canvasId, int width, int height, const ContextOptions& options) {
  if (context > 0) return true;
  canvasSelector = "#" + canvasId;
  EmscriptenWebGLContextAttributes attr;
  emscripten_webgl_init_context_attributes(&attr);
  attr.alpha = EM_FALSE;
  attr.depth = EM_TRUE;
  attr.stencil = EM_FALSE;
  attr.antialias = options.antialias ? EM_TRUE : EM_FALSE;
  attr.premultipliedAlpha = EM_TRUE;
  attr.preserveDrawingBuffer = EM_FALSE;
  attr.powerPreference = options.highPerformance ? EM_WEBGL_POWER_PREFERENCE_HIGH_PERFORMANCE : EM_WEBGL_POWER_PREFERENCE_DEFAULT;
  attr.majorVersion = 2;
  attr.minorVersion = 0;
  attr.enableExtensionsByDefault = EM_TRUE;
  context = emscripten_webgl_create_context(canvasSelector.c_str(), &attr);
  if (context <= 0) return false;
  emscripten_webgl_make_context_current(context);
  emscripten_set_canvas_element_size(canvasSelector.c_str(), width, height);
  return true;
}

bool hasContext() { return context > 0; }

void resizeDrawingBuffer(int width, int height) {
  if (context <= 0) return;
  int w = 0, h = 0;
  emscripten_get_canvas_element_size(canvasSelector.c_str(), &w, &h);
  if (w != width || h != height) emscripten_set_canvas_element_size(canvasSelector.c_str(), width, height);
}

uint32_t screenFramebuffer() { return 0; }

bool texImageCanvas(int canvasElement, bool flipY, bool srgb, int& width, int& height) {
  int size[2] = {0, 0};
  if (!fsg_tex_image(1, canvasElement, flipY ? 1 : 0, srgb ? 1 : 0, size)) return false;
  width = size[0];
  height = size[1];
  return true;
}

bool texImageImage(int image, bool flipY, bool srgb, int& width, int& height) {
  int size[2] = {0, 0};
  if (!fsg_tex_image(0, image, flipY ? 1 : 0, srgb ? 1 : 0, size)) return false;
  width = size[0];
  height = size[1];
  return true;
}

bool savePng(const std::string&, int, int) { return false; }

std::string rendererName() {
  char* s = fsg_renderer_name();
  std::string out = s ? s : "";
  std::free(s);
  return out;
}

}  // namespace fs::plat::gl
