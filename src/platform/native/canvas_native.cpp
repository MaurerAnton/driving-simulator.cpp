// SPDX-License-Identifier: AGPL-3.0-only
/**
 * canvas.h on this machine: nothing is painted.  The calls are counted (a test
 * asks whether something was drawn at all), a text is measured roughly from the
 * size its font names, and every context remembers the last texts it was asked
 * to write (a test reads them).
 */
#include <map>
#include <string>
#include <vector>

#include "platform/canvas.h"
#include "platform/native/canvas_native.h"

namespace fs {

namespace {

struct Ctx {
  int canvas = 0;
  double fontPx = 10;
  long calls = 0;
  std::vector<std::string> texts;
};

std::vector<Ctx>& contexts() {
  static auto* c = new std::vector<Ctx>(1);   // (never destroyed: canvases of static objects go while the program ends)
  return *c;
}
std::map<int, int>& byCanvas() {
  static auto* m = new std::map<int, int>();
  return *m;
}
int gradients = 0;

Ctx* ctxOf(int h) { return h > 0 && h < static_cast<int>(contexts().size()) ? &contexts()[static_cast<size_t>(h)] : nullptr; }

/** how many characters a UTF-8 text has */
size_t characters(const char* s) {
  size_t n = 0;
  for (; *s; s++)
    if ((static_cast<unsigned char>(*s) & 0xc0) != 0x80) n++;
  return n;
}

}  // namespace

namespace canvasBackend {

int context(int canvasElement) {
  auto it = byCanvas().find(canvasElement);
  if (it != byCanvas().end()) return it->second;
  Ctx c;
  c.canvas = canvasElement;
  contexts().push_back(c);
  int h = static_cast<int>(contexts().size()) - 1;
  byCanvas()[canvasElement] = h;
  return h;
}

void num(int ctx, int, double, double, double, double, double, double, double, double) {
  if (Ctx* c = ctxOf(ctx)) c->calls++;
}

void str(int ctx, int op, const char* s, double, double, double) {
  Ctx* c = ctxOf(ctx);
  if (!c) return;
  c->calls++;
  if (op == Font) {
    // "600 13px system-ui": the number before "px"
    std::string f = s;
    size_t px = f.find("px");
    if (px != std::string::npos) {
      size_t b = px;
      while (b > 0 && (std::isdigit(static_cast<unsigned char>(f[b - 1])) || f[b - 1] == '.')) b--;
      if (b < px) c->fontPx = std::strtod(f.c_str() + b, nullptr);
    }
  } else if (op == FillText || op == FillTextMax || op == StrokeText) {
    c->texts.push_back(s);
    if (c->texts.size() > 400) c->texts.erase(c->texts.begin(), c->texts.begin() + 200);
  }
}

void lineDash(int ctx, const double*, int) {
  if (Ctx* c = ctxOf(ctx)) c->calls++;
}
void polyline(int ctx, const double*, int, bool) {
  if (Ctx* c = ctxOf(ctx)) c->calls++;
}
void drawImage(int ctx, bool, int, const double*, int) {
  if (Ctx* c = ctxOf(ctx)) c->calls++;
}
void measure(int ctx, const char* text, double out[5]) {
  Ctx* c = ctxOf(ctx);
  double px = c ? c->fontPx : 10;
  out[0] = 0.55 * px * static_cast<double>(characters(text));
  out[1] = 0.74 * px;
  out[2] = 0.2 * px;
  out[3] = 0;
  out[4] = out[0];
}
int gradient(int, bool, double, double, double, double, double, double) { return ++gradients; }
void gradientFree(int) {}
void getImageData(int, int, int, int, int, uint8_t*) {}
void putImageData(int ctx, const uint8_t*, int, int, int, int) {
  if (Ctx* c = ctxOf(ctx)) c->calls++;
}
std::string toDataURL(int, const char*, double) { return "data:,"; }

}  // namespace canvasBackend

Task<Image> loadImage(std::string) { return resolved(Image()); }
Task<Image> decodeImage(Bytes, std::string) { return resolved(Image()); }
void freeImage(Image& image) { image = Image(); }

namespace canvasNative {

long calls(const Ctx2D& g) {
  Ctx* c = ctxOf(g.handle());
  return c ? c->calls : 0;
}
std::vector<std::string> texts(const Ctx2D& g) {
  Ctx* c = ctxOf(g.handle());
  return c ? c->texts : std::vector<std::string>();
}
void forget(const Ctx2D& g) {
  if (Ctx* c = ctxOf(g.handle())) {
    c->calls = 0;
    c->texts.clear();
  }
}

}  // namespace canvasNative

}  // namespace fs
