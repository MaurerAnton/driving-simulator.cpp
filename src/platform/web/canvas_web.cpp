// SPDX-License-Identifier: AGPL-3.0-only
/** canvas.h in the browser: the real 2D context (platform/web/library_fahrsim.js). */
#include <cstdlib>

#include "platform/canvas.h"

extern "C" {
int fsc_context(int canvas);
void fsc_num(int c, int op, double a, double b, double d, double e, double f, double g, double h, double i);
void fsc_str(int c, int op, const char* s, double a, double b, double d);
void fsc_line_dash(int c, const double* v, int n);
void fsc_polyline(int c, const double* xy, int n, int close);
void fsc_draw_image(int c, int fromCanvas, int source, const double* v, int n);
void fsc_measure(int c, const char* text, double* out);
int fsc_gradient(int c, int radial, double a, double b, double d, double e, double f, double g);
void fsc_get_image_data(int c, int x, int y, int w, int h, uint8_t* out);
void fsc_put_image_data(int c, const uint8_t* data, int w, int h, int x, int y);
char* fsc_to_data_url(int canvas, const char* type, double quality);
}

namespace fs::canvasBackend {

int context(int canvasElement) { return fsc_context(canvasElement); }
void num(int ctx, int op, double a, double b, double c, double d, double e, double f, double g, double h) { fsc_num(ctx, op, a, b, c, d, e, f, g, h); }
void str(int ctx, int op, const char* s, double a, double b, double c) { fsc_str(ctx, op, s, a, b, c); }
void lineDash(int ctx, const double* segments, int n) { fsc_line_dash(ctx, segments, n); }
void polyline(int ctx, const double* xy, int n, bool close) { fsc_polyline(ctx, xy, n, close ? 1 : 0); }
void drawImage(int ctx, bool fromCanvas, int source, const double* v, int n) { fsc_draw_image(ctx, fromCanvas ? 1 : 0, source, v, n); }
void measure(int ctx, const char* text, double out[5]) { fsc_measure(ctx, text, out); }
int gradient(int ctx, bool radial, double a, double b, double c, double d, double e, double f) { return fsc_gradient(ctx, radial ? 1 : 0, a, b, c, d, e, f); }
void getImageData(int ctx, int x, int y, int w, int h, uint8_t* out) { fsc_get_image_data(ctx, x, y, w, h, out); }
void putImageData(int ctx, const uint8_t* data, int w, int h, int x, int y) { fsc_put_image_data(ctx, data, w, h, x, y); }
std::string toDataURL(int canvasElement, const char* type, double quality) {
  char* s = fsc_to_data_url(canvasElement, type, quality);
  std::string out = s ? s : "";
  std::free(s);
  return out;
}

}  // namespace fs::canvasBackend
