// SPDX-License-Identifier: AGPL-3.0-only
/**
 * platform/glhost.h on this machine: an OpenGL ES 3 context that draws off screen
 * (EGL without a window), and its picture written as a PNG file – the game's
 * pictures without a browser, for development and for the tests that look at them.
 */
#include "platform/glhost.h"
#include "platform/native/canvas_native.h"
#include "platform/native/gl_native.h"

#include <cstdio>
#include <cstring>
#include <vector>

#if defined(FS_NATIVE_GL)
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES3/gl3.h>
#endif
#if defined(FS_HAVE_PNG)
#include <png.h>
#endif

namespace fs::plat::gl {

#if defined(FS_NATIVE_GL)

namespace {

struct Host {
  EGLDisplay display = EGL_NO_DISPLAY;
  EGLContext context = EGL_NO_CONTEXT;
  EGLSurface surface = EGL_NO_SURFACE;
  bool ok = false;
  /** the context is someone else's (a window's: platform/sdl) – no EGL of our own */
  bool adopted = false;
  GLuint fbo = 0, color = 0, depth = 0;
  int width = 0, height = 0;
};

Host& host() {
  static Host h;
  return h;
}

void makeBuffers(int width, int height) {
  Host& h = host();
  if (h.fbo && h.width == width && h.height == height) return;
  if (!h.fbo) {
    glGenFramebuffers(1, &h.fbo);
    glGenTextures(1, &h.color);
    glGenRenderbuffers(1, &h.depth);
  }
  h.width = width;
  h.height = height;
  glBindTexture(GL_TEXTURE_2D, h.color);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glBindTexture(GL_TEXTURE_2D, 0);
  glBindRenderbuffer(GL_RENDERBUFFER, h.depth);
  glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, width, height);
  glBindFramebuffer(GL_FRAMEBUFFER, h.fbo);
  glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, h.color, 0);
  glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, h.depth);
  if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) std::fprintf(stderr, "fahrsim: the off-screen picture of %d×%d is not complete\n", width, height);
}

}  // namespace

namespace native {

void adoptCurrentContext() {
  Host& h = host();
  h.adopted = true;
  h.ok = true;
}

void drawingBuffer(uint32_t& fbo, int& width, int& height) {
  const Host& h = host();
  fbo = h.fbo;
  width = h.width;
  height = h.height;
}

}  // namespace native

bool createContext(const std::string&, int width, int height, const ContextOptions&) {
  Host& h = host();
  if (h.ok) {
    makeBuffers(width, height);
    return true;
  }
  // a display without a window system: Mesa's "surfaceless" platform, else the default display
  const char* clientExt = eglQueryString(EGL_NO_DISPLAY, EGL_EXTENSIONS);
  if (clientExt && std::strstr(clientExt, "EGL_MESA_platform_surfaceless")) {
    auto getPlatformDisplay = reinterpret_cast<PFNEGLGETPLATFORMDISPLAYEXTPROC>(eglGetProcAddress("eglGetPlatformDisplayEXT"));
    if (getPlatformDisplay) h.display = getPlatformDisplay(EGL_PLATFORM_SURFACELESS_MESA, EGL_DEFAULT_DISPLAY, nullptr);
  }
  if (h.display == EGL_NO_DISPLAY) h.display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
  EGLint major = 0, minor = 0;
  if (h.display == EGL_NO_DISPLAY || !eglInitialize(h.display, &major, &minor)) {
    std::fprintf(stderr, "fahrsim: no EGL display (no pictures)\n");
    return false;
  }
  eglBindAPI(EGL_OPENGL_ES_API);
  const EGLint configAttribs[] = {EGL_SURFACE_TYPE, EGL_PBUFFER_BIT, EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT, EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8, EGL_NONE};
  EGLConfig config = nullptr;
  EGLint count = 0;
  if (!eglChooseConfig(h.display, configAttribs, &config, 1, &count) || count < 1) {
    // (a surfaceless display may have no pbuffer configuration: any configuration that draws with ES 3 will do)
    const EGLint any[] = {EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT, EGL_NONE};
    if (!eglChooseConfig(h.display, any, &config, 1, &count) || count < 1) {
      std::fprintf(stderr, "fahrsim: no OpenGL ES 3 configuration (no pictures)\n");
      return false;
    }
  }
  const EGLint contextAttribs[] = {EGL_CONTEXT_CLIENT_VERSION, 3, EGL_NONE};
  h.context = eglCreateContext(h.display, config, EGL_NO_CONTEXT, contextAttribs);
  if (h.context == EGL_NO_CONTEXT) {
    std::fprintf(stderr, "fahrsim: no OpenGL ES 3 context (no pictures)\n");
    return false;
  }
  // without a surface where the driver allows it, else a 1×1 pbuffer: the picture is our own framebuffer either way
  if (!eglMakeCurrent(h.display, EGL_NO_SURFACE, EGL_NO_SURFACE, h.context)) {
    const EGLint pb[] = {EGL_WIDTH, 1, EGL_HEIGHT, 1, EGL_NONE};
    h.surface = eglCreatePbufferSurface(h.display, config, pb);
    if (h.surface == EGL_NO_SURFACE || !eglMakeCurrent(h.display, h.surface, h.surface, h.context)) {
      std::fprintf(stderr, "fahrsim: the OpenGL ES context cannot be made current (no pictures)\n");
      return false;
    }
  }
  h.ok = true;
  makeBuffers(width, height);
  return true;
}

bool hasContext() { return host().ok; }

void resizeDrawingBuffer(int width, int height) {
  if (host().ok) makeBuffers(width, height);
}

uint32_t screenFramebuffer() { return host().fbo; }

namespace {
/** RGBA pixels (not premultiplied, as WebGL takes a canvas by default) into the bound texture */
bool upload(const std::vector<uint8_t>& px, bool srgb, int width, int height) {
  glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
  glTexImage2D(GL_TEXTURE_2D, 0, srgb ? GL_SRGB8_ALPHA8 : GL_RGBA8, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, px.data());
  return true;
}
}  // namespace

bool texImageCanvas(int canvasElement, bool flipY, bool srgb, int& width, int& height) {
  std::vector<uint8_t> px;
  int w = 0, h = 0;
  // (the 2D canvas of this host paints for real: platform/native/canvas_native.cpp)
  if (!canvasNative::pixels(canvasElement, px, w, h, false, flipY)) return false;
  width = w;
  height = h;
  return upload(px, srgb, w, h);
}
bool texImageImage(int image, bool flipY, bool srgb, int& width, int& height) {
  std::vector<uint8_t> px;
  int w = 0, h = 0;
  if (!canvasNative::imagePixels(image, px, w, h, false, flipY)) return false;
  width = w;
  height = h;
  return upload(px, srgb, w, h);
}

/** which driver draws ("Mali-G52 (Panfrost)", "llvmpipe" …): for the tools' output */
std::string rendererName() {
  if (!host().ok) return "none";
  const char* r = reinterpret_cast<const char*>(glGetString(GL_RENDERER));
  return r ? r : "?";
}

bool savePng(const std::string& path, int width, int height) {
#if defined(FS_HAVE_PNG)
  Host& h = host();
  if (!h.ok || width <= 0 || height <= 0) return false;
  std::vector<uint8_t> px(static_cast<size_t>(width) * static_cast<size_t>(height) * 4);
  glBindFramebuffer(GL_FRAMEBUFFER, h.fbo);
  glPixelStorei(GL_PACK_ALIGNMENT, 1);
  glReadPixels(0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, px.data());
  FILE* f = std::fopen(path.c_str(), "wb");
  if (!f) return false;
  png_structp png = png_create_write_struct(PNG_LIBPNG_VER_STRING, nullptr, nullptr, nullptr);
  png_infop info = png ? png_create_info_struct(png) : nullptr;
  if (!png || !info || setjmp(png_jmpbuf(png))) {
    if (png) png_destroy_write_struct(&png, &info);
    std::fclose(f);
    return false;
  }
  png_init_io(png, f);
  png_set_IHDR(png, info, static_cast<png_uint_32>(width), static_cast<png_uint_32>(height), 8, PNG_COLOR_TYPE_RGB, PNG_INTERLACE_NONE, PNG_COMPRESSION_TYPE_DEFAULT,
               PNG_FILTER_TYPE_DEFAULT);
  png_set_compression_level(png, 3);
  png_write_info(png, info);
  std::vector<uint8_t> row(static_cast<size_t>(width) * 3);
  // (OpenGL's first row is the bottom one)
  for (int y = height - 1; y >= 0; y--) {
    const uint8_t* src = px.data() + static_cast<size_t>(y) * static_cast<size_t>(width) * 4;
    for (int x = 0; x < width; x++) {
      row[static_cast<size_t>(x) * 3] = src[x * 4];
      row[static_cast<size_t>(x) * 3 + 1] = src[x * 4 + 1];
      row[static_cast<size_t>(x) * 3 + 2] = src[x * 4 + 2];
    }
    png_write_row(png, row.data());
  }
  png_write_end(png, nullptr);
  png_destroy_write_struct(&png, &info);
  std::fclose(f);
  return true;
#else
  (void)path;
  (void)width;
  (void)height;
  return false;
#endif
}

#else  // no OpenGL ES on this machine

namespace native {
void adoptCurrentContext() {}
void drawingBuffer(uint32_t& fbo, int& width, int& height) { fbo = 0, width = height = 0; }
}  // namespace native

bool createContext(const std::string&, int, int, const ContextOptions&) { return false; }
bool hasContext() { return false; }
void resizeDrawingBuffer(int, int) {}
uint32_t screenFramebuffer() { return 0; }
bool texImageCanvas(int, bool, bool, int&, int&) { return false; }
bool texImageImage(int, bool, bool, int&, int&) { return false; }
std::string rendererName() { return "none"; }
bool savePng(const std::string&, int, int) { return false; }

#endif

}  // namespace fs::plat::gl
