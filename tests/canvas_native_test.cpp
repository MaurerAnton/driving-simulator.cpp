// SPDX-License-Identifier: AGPL-3.0-only
// LINK: platform core
/**
 * The native 2D canvas (platform/native/canvas_native.cpp) paints for real: shapes,
 * strokes, transforms, clips, gradients, pictures, SVG and text are drawn and their
 * pixels looked at.  The counting of calls and the remembered texts (what other tests
 * read) stay.
 *
 *   tools/t.sh canvas_native_test [folder]   – with a folder: the pictures as PNG files there
 */
#include <cassert>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

#include "platform/canvas.h"
#include "platform/native/canvas_native.h"

using namespace fs;

static std::string outDir;
static int failures = 0;

#define OK(cond, what)                                    \
  do {                                                    \
    if (!(cond)) {                                        \
      std::printf("FAIL %s:%d %s\n", __FILE__, __LINE__, what); \
      failures++;                                         \
    }                                                     \
  } while (0)

struct Px {
  int r, g, b, a;
};
static Px at(const Ctx2D& g, int x, int y) {
  const ImageData d = g.getImageData(x, y, 1, 1);
  return {d.data[0], d.data[1], d.data[2], d.data[3]};
}
static bool near(const Px& p, int r, int g, int b, int a, int tol = 3) {
  const bool ok = std::abs(p.r - r) <= tol && std::abs(p.g - g) <= tol && std::abs(p.b - b) <= tol && std::abs(p.a - a) <= tol;
  if (!ok) std::printf("  pixel %d %d %d %d, not %d %d %d %d\n", p.r, p.g, p.b, p.a, r, g, b, a);
  return ok;
}
static void save(const Canvas& c, const char* name) {
  if (outDir.empty()) return;
  const std::string path = outDir + "/" + name + ".png";
  OK(canvasNative::savePng(c, path), "the PNG is written");
  std::printf("  %s\n", path.c_str());
}
/** the inked box of what is not transparent: x0 y0 x1 y1 (x1 < x0: nothing) */
static std::vector<int> inked(const Ctx2D& g, int w, int h) {
  const ImageData d = g.getImageData(0, 0, w, h);
  int x0 = w, y0 = h, x1 = -1, y1 = -1;
  for (int y = 0; y < h; y++)
    for (int x = 0; x < w; x++)
      if (d.data[(static_cast<size_t>(y) * static_cast<size_t>(w) + static_cast<size_t>(x)) * 4 + 3] > 64) {
        x0 = std::min(x0, x), y0 = std::min(y0, y), x1 = std::max(x1, x), y1 = std::max(y1, y);
      }
  return {x0, y0, x1, y1};
}

static void testShapes() {
  const Canvas c(200, 100);
  const Ctx2D g = c.getContext2D();
  // a blank canvas is transparent
  OK(near(at(g, 5, 5), 0, 0, 0, 0), "blank");
  g.fillStyle("#ff0000");
  g.fillRect(10, 10, 50, 30);
  OK(near(at(g, 20, 20), 255, 0, 0, 255), "fillRect");
  OK(near(at(g, 5, 5), 0, 0, 0, 0), "outside fillRect");
  OK(near(at(g, 59, 39), 255, 0, 0, 255) && near(at(g, 60, 40), 0, 0, 0, 0), "fillRect's edges on the pixels");
  // a circle (arc)
  g.fillStyle("rgb(0, 0, 255)");
  g.beginPath();
  g.arc(150, 50, 30, 0, 2 * M_PI);
  g.fill();
  OK(near(at(g, 150, 50), 0, 0, 255, 255), "circle: middle");
  OK(near(at(g, 150 + 27, 50), 0, 0, 255, 255), "circle: inside the rim");
  OK(near(at(g, 150 + 33, 50), 0, 0, 0, 0), "circle: outside");
  OK(near(at(g, 150 + 23, 50 + 23), 0, 0, 0, 0), "circle: not a square");
  // even-odd: a hole; non-zero (both clockwise): none
  g.fillStyle("#00ff00");
  g.beginPath();
  g.rect(70, 50, 40, 40);
  g.rect(80, 60, 20, 20);
  g.fillEvenOdd();
  OK(near(at(g, 72, 52), 0, 255, 0, 255) && near(at(g, 90, 70), 0, 0, 0, 0), "evenodd leaves the hole");
  g.fill();
  OK(near(at(g, 90, 70), 0, 255, 0, 255), "nonzero fills it");
  // clearRect
  g.clearRect(15, 15, 10, 10);
  OK(near(at(g, 20, 20), 0, 0, 0, 0) && near(at(g, 12, 12), 255, 0, 0, 255), "clearRect");
  save(c, "shapes");
  // the calls are still counted
  OK(canvasNative::calls(g) > 10, "calls counted");
}

static void testStrokes() {
  const Canvas c(200, 120);
  const Ctx2D g = c.getContext2D();
  g.strokeStyle("#000");
  g.lineWidth(10);
  // butt: ends at x = 50; square: 5 further
  g.beginPath();
  g.moveTo(20, 20);
  g.lineTo(50, 20);
  g.stroke();
  OK(near(at(g, 48, 20), 0, 0, 0, 255) && near(at(g, 53, 20), 0, 0, 0, 0), "butt cap");
  OK(near(at(g, 30, 16), 0, 0, 0, 255) && near(at(g, 30, 27), 0, 0, 0, 0), "line width");
  g.lineCap("square");
  g.beginPath();
  g.moveTo(20, 50);
  g.lineTo(50, 50);
  g.stroke();
  OK(near(at(g, 53, 50), 0, 0, 0, 255), "square cap");
  g.lineCap("round");
  g.beginPath();
  g.moveTo(20, 80);
  g.lineTo(50, 80);
  g.stroke();
  OK(near(at(g, 53, 80), 0, 0, 0, 255) && near(at(g, 54, 84), 0, 0, 0, 0), "round cap");
  // dashes: 10 on, 10 off
  g.lineCap("butt");
  g.setLineDash({10, 10});
  g.lineWidth(4);
  g.beginPath();
  g.moveTo(80, 100);
  g.lineTo(180, 100);
  g.stroke();
  g.setLineDash({});
  OK(near(at(g, 85, 100), 0, 0, 0, 255) && near(at(g, 95, 100), 0, 0, 0, 0) && near(at(g, 105, 100), 0, 0, 0, 255), "dashes");
  // a transformed stroke: the width is scaled with it
  g.save();
  g.translate(120, 20);
  g.scale(3, 3);
  g.lineWidth(2);
  g.beginPath();
  g.moveTo(0, 5);
  g.lineTo(20, 5);
  g.stroke();
  g.restore();
  OK(near(at(g, 150, 35 + 2), 0, 0, 0, 255) && near(at(g, 150, 35 + 4), 0, 0, 0, 0), "stroke under scale(3): 6 px wide");
  // strokeRect
  g.lineWidth(2);
  g.strokeStyle("#f00");
  g.strokeRect(120, 50, 40, 30);
  OK(near(at(g, 120, 65), 255, 0, 0, 255) && near(at(g, 140, 65), 0, 0, 0, 0), "strokeRect");
  // a round join
  save(c, "strokes");
}

static void testTransformsClipAlpha() {
  const Canvas c(200, 200);
  const Ctx2D g = c.getContext2D();
  // rotated square: corner points of a 45° diamond
  g.save();
  g.translate(50, 50);
  g.rotate(M_PI / 4);
  g.fillStyle("#000");
  g.fillRect(-20, -20, 40, 40);
  g.restore();
  OK(near(at(g, 50, 50 - 26), 0, 0, 0, 255) && near(at(g, 50 - 22, 50 - 22), 0, 0, 0, 0), "rotated square is a diamond");
  // after restore the transform is the identity again
  g.fillStyle("#00f");
  g.fillRect(0, 190, 10, 10);
  OK(near(at(g, 5, 195), 0, 0, 255, 255), "restore");
  // globalAlpha
  g.save();
  g.globalAlpha(0.5);
  g.fillStyle("#ff0000");
  g.fillRect(100, 0, 20, 20);
  g.restore();
  OK(near(at(g, 110, 10), 255, 0, 0, 128, 3), "globalAlpha 0.5");
  // a clip: a circle; the rectangle filled over it shows only inside
  g.save();
  g.beginPath();
  g.arc(150, 150, 20, 0, 2 * M_PI);
  g.clip();
  g.fillStyle("#0a0");
  g.fillRect(100, 100, 100, 100);
  g.restore();
  OK(near(at(g, 150, 150), 0, 170, 0, 255) && near(at(g, 150, 125), 0, 0, 0, 0) && near(at(g, 120, 120), 0, 0, 0, 0), "clip");
  // the clip went with restore (and the fill style with it)
  g.fillRect(100, 100, 5, 5);
  OK(near(at(g, 102, 102), 0, 0, 255, 255), "clip restored");
  // setTransform, ellipse with rotation
  g.setTransform(1, 0, 0, 1, 0, 0);
  g.fillStyle("#808");
  g.beginPath();
  g.ellipse(50, 150, 40, 10, 0, 0, 2 * M_PI);
  g.fill();
  OK(near(at(g, 85, 150), 136, 0, 136, 255) && near(at(g, 50, 165), 0, 0, 0, 0), "ellipse");
  // destination-out cuts a hole
  g.globalCompositeOperation("destination-out");
  g.fillRect(45, 145, 10, 10);
  g.globalCompositeOperation("source-over");
  OK(near(at(g, 50, 150), 0, 0, 0, 0), "destination-out");
  // roundRect and arcTo
  g.fillStyle("#fa0");
  g.beginPath();
  g.roundRect(120, 30, 60, 40, 12);
  g.fill();
  OK(near(at(g, 150, 50), 255, 170, 0, 255) && near(at(g, 121, 31), 0, 0, 0, 0), "roundRect has round corners");
  save(c, "transforms");
}

static void testGradients() {
  const Canvas c(100, 60);
  const Ctx2D g = c.getContext2D();
  const CanvasGradient lin = g.createLinearGradient(0, 0, 100, 0);
  lin.addColorStop(0, "#000000");
  lin.addColorStop(1, "#ffffff");
  g.fillStyle(lin);
  g.fillRect(0, 0, 100, 30);
  const Px mid = at(g, 50, 10);
  OK(std::abs(mid.r - 128) < 8 && mid.r == mid.g && mid.a == 255, "linear gradient: grey in the middle");
  OK(at(g, 2, 10).r < 12 && at(g, 97, 10).r > 243, "linear gradient: ends");
  const CanvasGradient rad = g.createRadialGradient(50, 45, 0, 50, 45, 15);
  rad.addColorStop(0, "#ff0000");
  rad.addColorStop(1, "rgba(255,0,0,0)");
  g.fillStyle(rad);
  g.fillRect(0, 30, 100, 30);
  OK(at(g, 50, 45).a > 240 && at(g, 50 + 8, 45).a < 160 && at(g, 50 + 17, 45).a < 8, "radial gradient");
  save(c, "gradients");
}

static void testText() {
  const Canvas c(320, 120);
  const Ctx2D g = c.getContext2D();
  g.font("bold 40px Arial, sans-serif");
  const TextMetrics wide = g.measureText("WWW");
  const TextMetrics narrow = g.measureText("iii");
  OK(wide.width > 2 * narrow.width, "real widths: W is wider than i");
  OK(wide.width > 60 && wide.width < 140, "40px WWW");
  const TextMetrics hg = g.measureText("Hg");
  OK(hg.actualBoundingBoxAscent > 25 && hg.actualBoundingBoxAscent < 35 && hg.actualBoundingBoxDescent > 5 && hg.actualBoundingBoxDescent < 14, "the inked box of Hg");
  g.fillStyle("#000");
  g.textBaseline("alphabetic");
  g.fillText("Hg", 10, 60);
  std::vector<int> box = inked(g, 320, 120);
  OK(box[0] >= 10 && box[0] <= 14 && box[1] >= 60 - 35 && box[1] <= 60 - 25 && box[3] > 62 && box[3] < 75, "Hg inked where measured");
  // centred, middle baseline: the ink is around the point
  g.clearRect(0, 0, 320, 120);
  g.textAlign("center");
  g.textBaseline("middle");
  g.font("700 30px \"DIN Alternate\", \"Roboto Condensed\", sans-serif");
  g.fillText("30", 160, 60);
  box = inked(g, 320, 120);
  OK(std::abs((box[0] + box[2]) / 2 - 160) <= 2 && std::abs((box[1] + box[3]) / 2 - 60) <= 4, "centred text around its point");
  // maxWidth squeezes
  g.clearRect(0, 0, 320, 120);
  g.textAlign("left");
  g.textBaseline("top");
  g.font("20px sans-serif");
  g.fillText("Baunataler Straße", 10, 10, 60);
  box = inked(g, 320, 120);
  OK(box[2] <= 72 && box[2] > 50, "maxWidth");
  // umlauts and the remembered texts
  OK(canvasNative::texts(g).size() == 3 && canvasNative::texts(g)[2] == "Baunataler Straße", "texts remembered");
  // strokeText draws an outline
  g.font("bold 60px sans-serif");
  g.lineWidth(2);
  g.strokeStyle("#c00");
  g.strokeText("O", 200, 20);
  const ImageData d = g.getImageData(200, 20, 60, 70);
  int red = 0;
  for (size_t i = 0; i < d.data.size(); i += 4)
    if (d.data[i] > 150 && d.data[i + 3] > 150) red++;
  OK(red > 50 && red < 1500, "strokeText: an outline, not a filled O");
  save(c, "text");
}

static void testPictures() {
  // canvas → canvas, scaled
  const Canvas src(2, 2);
  const Ctx2D s = src.getContext2D();
  s.fillStyle("#f00");
  s.fillRect(0, 0, 1, 1);
  s.fillStyle("#0f0");
  s.fillRect(1, 0, 1, 1);
  s.fillStyle("#00f");
  s.fillRect(0, 1, 1, 1);
  s.fillStyle("#fff");
  s.fillRect(1, 1, 1, 1);
  const Canvas c(120, 80);
  const Ctx2D g = c.getContext2D();
  g.imageSmoothingEnabled(false);
  g.drawImage(src, 0, 0, 40, 40);
  OK(near(at(g, 10, 10), 255, 0, 0, 255) && near(at(g, 30, 10), 0, 255, 0, 255) && near(at(g, 10, 30), 0, 0, 255, 255) && near(at(g, 30, 30), 255, 255, 255, 255), "drawImage scaled");
  g.imageSmoothingEnabled(true);
  g.drawImage(src, 1, 0, 1, 1, 40, 0, 20, 20);
  OK(near(at(g, 50, 10), 0, 255, 0, 255), "drawImage of a source rectangle");
  // SVG from a data address
  const std::string svg = "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"10\" height=\"10\"><circle cx=\"5\" cy=\"5\" r=\"5\" fill=\"#00ff00\"/></svg>";
  Task<Image> t = loadImage("data:image/svg+xml;charset=utf-8," + svg);
  OK(t.done(), "a data address is decoded at once");
  Image img = t.get();
  OK(img && img.width == 10 && img.height == 10, "SVG picture and its size");
  g.drawImage(img, 70, 10, 40, 40);
  OK(near(at(g, 90, 30), 0, 255, 0, 255) && near(at(g, 71, 11), 0, 0, 0, 0), "SVG drawn at 40 px");
  freeImage(img);
  OK(!img, "freed");
  // a PNG: the canvas's own picture back
  const std::string url = src.toDataURL();
  OK(url.rfind("data:image/png;base64,", 0) == 0, "toDataURL");
  Image png = loadImage(url).get();
  OK(png && png.width == 2, "PNG decoded");
  g.imageSmoothingEnabled(false);
  g.drawImage(png, 0, 50, 20, 20);
  OK(near(at(g, 15, 65), 255, 255, 255, 255), "PNG drawn");
  freeImage(png);
  // getImageData / putImageData
  ImageData d = g.getImageData(0, 0, 4, 4);
  for (size_t i = 0; i < d.data.size(); i += 4) d.data[i] = 7, d.data[i + 1] = 8, d.data[i + 2] = 9, d.data[i + 3] = 255;
  g.putImageData(d, 100, 60);
  OK(near(at(g, 101, 61), 7, 8, 9, 255, 0), "putImageData");
  save(c, "pictures");
  // the texture's pixels: flipped, and a new size wipes the canvas
  std::vector<uint8_t> px;
  int w = 0, h = 0;
  OK(canvasNative::pixels(src.el().handle(), px, w, h, false, true) && w == 2 && h == 2 && px[0] == 0 && px[2] == 255, "pixels, bottom row first");
  const uint64_t v = canvasNative::version(s);
  src.width(2);
  OK(canvasNative::pixels(src.el().handle(), px, w, h) && px[3] == 0, "a new width wipes the canvas");
  OK(canvasNative::version(s) != v, "and counts as a change");
}

int main(int argc, char** argv) {
  if (argc > 1) outDir = argv[1];
  testShapes();
  testStrokes();
  testTransformsClipAlpha();
  testGradients();
  testText();
  testPictures();
  if (failures) {
    std::printf("canvas_native_test: %d failed\n", failures);
    return 1;
  }
  std::printf("canvas_native_test: ok\n");
  return 0;
}
